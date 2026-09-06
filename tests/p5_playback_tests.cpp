#include "adapters/audio/IAudioPlayer.h"
#include "core/tts/ITtsEngine.h"
#include "services/PlaybackService.h"
#include "platform/FileIo.h"
#include "platform/UnicodePath.h"
#include <filesystem>
#include <limits>

#include "TestCheck.h"

#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

using namespace adayo;

namespace {
class FakeTtsEngine final : public ITtsEngine {
public:
    std::string Id() const override { return "fake"; }
    std::string RuntimeIdentity() const override { return runtime_identity; }
    std::string runtime_identity{"fake-runtime-v1"};
    bool IsLoaded() const noexcept override { return loaded_; }
    void Load(const TtsModelConfig& config) override {
        ++load_count;
        load_history.push_back(config.model_path);
        if (fail_load) throw std::runtime_error("load failed");
        loaded_ = true;
    }
    void Unload() noexcept override { loaded_ = false; ++unload_count; }
    AudioBuffer Synthesize(const TtsRequest& request) override {
        {
            std::unique_lock lock(mutex);
            ++synth_count;
            last_text = request.text;
            synth_started = true;
        }
        cv.notify_all();
        if (block_synthesis) {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return release_synthesis; });
        }
        AudioBuffer audio;
        audio.sample_rate = 16000;
        audio.channels = 1;
        audio.samples.assign(160, 0.1f);
        return audio;
    }

    std::atomic<int> synth_count{};
    std::atomic<int> load_count{};
    std::atomic<int> unload_count{};
    bool fail_load{false};
    std::vector<std::string> load_history;
    std::string last_text;
    bool block_synthesis{false};
    bool release_synthesis{false};
    bool synth_started{false};
    std::mutex mutex;
    std::condition_variable cv;

    bool WaitForSynthStart(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, timeout, [&] { return synth_started; });
    }

    void ReleaseSynthesis() {
        {
            std::lock_guard lock(mutex);
            release_synthesis = true;
        }
        cv.notify_all();
    }

private:
    bool loaded_{false};
};

class FakePlayer final : public IAudioPlayer {
public:
    void Play(const AudioBuffer& audio) override {
        REQUIRE(!audio.samples.empty());
        {
            std::lock_guard lock(mutex);
            ++play_count;
            playing = true;
        }
        cv.notify_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        {
            std::lock_guard lock(mutex);
            playing = false;
        }
        cv.notify_all();
    }
    void Pause() override { paused = true; }
    void Resume() override { paused = false; ++resume_count; }
    void Stop() override { ++stop_count; }

    bool WaitForPlayCount(int count, std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, timeout, [&] { return play_count >= count; });
    }

    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<int> play_count{};
    std::atomic<int> stop_count{};
    std::atomic<int> resume_count{};
    std::atomic<bool> playing{false};
    std::atomic<bool> paused{false};
};

PlaybackRequest MakePlaybackRequest(std::vector<PlaybackItem> items,
    std::chrono::milliseconds interval = std::chrono::milliseconds{0},
    double speed = 1.0,
    std::string model_id = "fake-model") {
    PlaybackRequest request;
    request.model_id = std::move(model_id);
    request.model_config.engine_id = "fake";
    request.model_config.model_path = request.model_id;
    request.items = std::move(items);
    request.interval = interval;
    request.speed = speed;
    return request;
}

void TestSequenceSkipsEmptyAndReturnsIdle() {
    auto* engine = new FakeTtsEngine();
    TtsService tts;
    tts.SetEngine(std::unique_ptr<ITtsEngine>(engine));
    FakePlayer player;
    PlaybackService playback(tts, player);

    playback.Play(MakePlaybackRequest({
        {{"first", "en-US", 0, 1.0}, 10, 2},
        {{"", "en-US", 0, 1.0}, 11, 2},
        {{"third", "en-US", 0, 1.0}, 12, 2},
    }, std::chrono::milliseconds{1}, 1.0));

    REQUIRE(playback.WaitUntilIdle(std::chrono::seconds{2}));
    REQUIRE(playback.State() == PlaybackState::Idle);
    REQUIRE(engine->synth_count == 2);
    REQUIRE(player.play_count == 2);
    REQUIRE(playback.CurrentRow() == 12);
}

void TestPauseResumeAndStopDoNotLeaveStaleState() {
    auto* engine = new FakeTtsEngine();
    TtsService tts;
    tts.SetEngine(std::unique_ptr<ITtsEngine>(engine));
    FakePlayer player;
    PlaybackService playback(tts, player);

    playback.Play(MakePlaybackRequest({{{"first", "en-US", 0, 1.0}, 1, 1}}));
    REQUIRE(player.WaitForPlayCount(1, std::chrono::seconds{2}));
    playback.Stop();
    REQUIRE(playback.WaitUntilIdle(std::chrono::seconds{2}));
    REQUIRE(playback.State() == PlaybackState::Idle);
    REQUIRE(player.stop_count >= 1);
}

void TestPauseDuringGeneratingWaitsBeforePlaying() {
    auto* engine = new FakeTtsEngine();
    engine->block_synthesis = true;
    TtsService tts;
    tts.SetEngine(std::unique_ptr<ITtsEngine>(engine));
    FakePlayer player;
    PlaybackService playback(tts, player);

    playback.Play(MakePlaybackRequest({{{"slow", "en-US", 0, 1.0}, 7, 3}}));
    REQUIRE(engine->WaitForSynthStart(std::chrono::seconds{2}));
    playback.Pause();
    REQUIRE(playback.State() == PlaybackState::Paused);
    engine->ReleaseSynthesis();
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    REQUIRE(player.play_count == 0);
    playback.Resume();
    REQUIRE(player.WaitForPlayCount(1, std::chrono::seconds{2}));
    REQUIRE(playback.WaitUntilIdle(std::chrono::seconds{2}));
}

void TestStopDuringIntervalExitsPromptly() {
    auto* engine = new FakeTtsEngine();
    TtsService tts;
    tts.SetEngine(std::unique_ptr<ITtsEngine>(engine));
    FakePlayer player;
    PlaybackService playback(tts, player);

    playback.Play(MakePlaybackRequest({
        {{"first", "en-US", 0, 1.0}, 1, 1},
        {{"second", "en-US", 0, 1.0}, 2, 1},
    }, std::chrono::seconds{3}, 1.0));
    REQUIRE(player.WaitForPlayCount(1, std::chrono::seconds{2}));
    const auto start = std::chrono::steady_clock::now();
    playback.Stop();
    REQUIRE(playback.WaitUntilIdle(std::chrono::milliseconds{500}));
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    REQUIRE(elapsed < 500);
    REQUIRE(player.play_count == 1);
}

void TestStopThenNewSequenceIgnoresOldWorkerState() {
    auto* engine = new FakeTtsEngine();
    engine->block_synthesis = true;
    TtsService tts;
    tts.SetEngine(std::unique_ptr<ITtsEngine>(engine));
    FakePlayer player;
    PlaybackService playback(tts, player);

    playback.Play(MakePlaybackRequest({{{"old", "en-US", 0, 1.0}, 1, 1}}, std::chrono::milliseconds{0}, 1.0, "old"));
    REQUIRE(engine->WaitForSynthStart(std::chrono::seconds{2}));
    playback.Stop();
    playback.Play(MakePlaybackRequest({{{"new", "en-US", 0, 1.0}, 2, 1}}, std::chrono::milliseconds{0}, 1.0, "new"));
    engine->ReleaseSynthesis();
    REQUIRE(playback.WaitUntilIdle(std::chrono::seconds{2}));
    REQUIRE(player.play_count == 1);
    REQUIRE(playback.CurrentRow() == 2);
}

void TestSameModelIsLoadedOnce() {
    auto* engine = new FakeTtsEngine();
    TtsService tts;
    tts.SetEngine(std::unique_ptr<ITtsEngine>(engine));
    FakePlayer player;
    PlaybackService playback(tts, player);

    PlaybackRequest request = MakePlaybackRequest({
        {{"one", "en-US", 0, 1.0}, 1, 1},
        {{"two", "en-US", 0, 1.0}, 2, 1},
    }, std::chrono::milliseconds{0}, 1.0, "voice-a");
    playback.Play(request);
    REQUIRE(playback.WaitUntilIdle(std::chrono::seconds{2}));
    playback.Play(std::move(request));
    REQUIRE(playback.WaitUntilIdle(std::chrono::seconds{2}));
    REQUIRE(engine->load_count == 1);
    REQUIRE(engine->synth_count == 4);
}

void TestModelSwitchLoadsOnlyOnVoiceChange() {
    auto* engine = new FakeTtsEngine();
    TtsService tts;
    tts.SetEngine(std::unique_ptr<ITtsEngine>(engine));
    FakePlayer player;
    PlaybackService playback(tts, player);

    auto play_voice = [&](const char* id) {
        PlaybackRequest request = MakePlaybackRequest({{{"text", "en-US", 0, 1.0}, 1, 1}},
            std::chrono::milliseconds{0}, 1.0, id);
        playback.Play(std::move(request));
        REQUIRE(playback.WaitUntilIdle(std::chrono::seconds{2}));
    };

    play_voice("en");
    play_voice("zh");
    play_voice("en");
    REQUIRE(engine->load_count == 3);
    REQUIRE(engine->unload_count == 2);
}

void TestEngineMismatchRejectedAtExecutionLayer() {
    auto* engine = new FakeTtsEngine();
    TtsService tts;
    tts.SetEngine(std::unique_ptr<ITtsEngine>(engine));
    TtsModelConfig config;
    config.engine_id = "other";
    bool threw = false;
    try {
        tts.EnsureModelLoaded("bad", config);
    } catch (const std::runtime_error& ex) {
        threw = std::string(ex.what()).find("ENGINE_MISMATCH") != std::string::npos;
    }
    REQUIRE(threw);
    REQUIRE(engine->load_count == 0);
}

void TestLoadFailureClearsActiveModelId() {
    auto* engine = new FakeTtsEngine();
    TtsService tts;
    tts.SetEngine(std::unique_ptr<ITtsEngine>(engine));
    TtsModelConfig ok;
    ok.engine_id = "fake";
    ok.model_path = "ok";
    tts.EnsureModelLoaded("ok", ok);
    REQUIRE(tts.ActiveModelId() == "ok");

    TtsModelConfig bad;
    bad.engine_id = "fake";
    bad.model_path = "bad";
    engine->fail_load = true;
    bool threw = false;
    try {
        tts.EnsureModelLoaded("bad", bad);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    REQUIRE(threw);
    REQUIRE(tts.ActiveModelId().empty());
    REQUIRE(!engine->IsLoaded());
}

void TestStalePlaybackRequestDoesNotLoadOldModel() {
    auto* engine = new FakeTtsEngine();
    engine->block_synthesis = true;
    TtsService tts;
    tts.SetEngine(std::unique_ptr<ITtsEngine>(engine));
    FakePlayer player;
    PlaybackService playback(tts, player);

    playback.Play(MakePlaybackRequest({{{"barrier", "en-US", 0, 1.0}, 0, 1}},
        std::chrono::milliseconds{0}, 1.0, "barrier"));
    REQUIRE(engine->WaitForSynthStart(std::chrono::seconds{2}));
    playback.Play(MakePlaybackRequest({{{"old", "en-US", 0, 1.0}, 1, 1}},
        std::chrono::milliseconds{0}, 1.0, "old"));
    playback.Play(MakePlaybackRequest({{{"new", "en-US", 0, 1.0}, 2, 1}},
        std::chrono::milliseconds{0}, 1.0, "new"));
    engine->ReleaseSynthesis();
    REQUIRE(playback.WaitUntilIdle(std::chrono::seconds{2}));
    REQUIRE(engine->load_history.size() == 2);
    REQUIRE(engine->load_history[0] == "barrier");
    REQUIRE(engine->load_history[1] == "new");
}

void TestRepeatedStopDuringSynthesis() {
    for (int count : {2, 10}) {
        auto* engine = new FakeTtsEngine();
        engine->block_synthesis = true;
        TtsService tts;
        tts.SetEngine(std::unique_ptr<ITtsEngine>(engine));
        FakePlayer player;
        PlaybackService playback(tts, player);
        playback.Play(MakePlaybackRequest({{{"blocked", "en-US", 0, 1.0}, 1, 1}}));
        REQUIRE(engine->WaitForSynthStart(std::chrono::seconds{2}));
        for (int i = 0; i < count; ++i) playback.Stop();
        engine->ReleaseSynthesis();
        REQUIRE(playback.WaitUntilIdle(std::chrono::seconds{2}));
        REQUIRE(playback.State() == PlaybackState::Idle);
        REQUIRE(player.play_count == 0);
    }
}

std::filesystem::path NewCacheTestRoot() {
    auto root=test::IsolatedRoot()/("adayo-cache-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(std::filesystem::create_directory(root));
    return root;
}
TtsModelConfig CacheTestConfig(const std::filesystem::path& root) {
    WriteBinaryFile(root/"model.onnx","model",5);
    WriteBinaryFile(root/"tokens.txt","tokens",6);
    TtsModelConfig config;
    config.engine_id="fake"; config.language_code="en-US";
    config.model_path=PathToUtf8(root/"model.onnx"); config.tokens_path=PathToUtf8(root/"tokens.txt");
    return config;
}
void TestCacheServiceIdentityAndRestart() {
    REQUIRE(Sha256("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const auto root=NewCacheTestRoot();
    auto config=CacheTestConfig(root);
    std::string key;
    std::vector<float> samples;
    {
        TtsService tts; tts.SetEngine(std::make_unique<FakeTtsEngine>()); tts.InitializeCache(root/"tts-v1",{});
        const TtsRequest request{"hello", "en-US",0,1.0};
        auto cold=tts.Prepare("voice",config,request); key=cold.timings.key; samples=cold.audio->samples;
        REQUIRE(cold.timings.source=="synth"); REQUIRE(cold.timings.synth_call_delta==1); REQUIRE(cold.timings.load_call_delta==1);
        auto warm=tts.Prepare("voice",config,request);
        REQUIRE(warm.timings.source=="memory"); REQUIRE(warm.timings.synth_call_delta==0); REQUIRE(warm.timings.load_call_delta==0);
        REQUIRE(warm.audio->samples==samples);
        for (auto variant : {TtsRequest{"hello!","en-US",0,1.0},TtsRequest{"hello","en-US",1,1.0},TtsRequest{"hello","en-US",0,1.1}})
            REQUIRE(tts.Prepare("voice",config,variant).timings.key!=key);
        const auto low=tts.Prepare("voice",config,{"slow","en-US",0,0.49});
        REQUIRE(low.timings.key==tts.Prepare("voice",config,{"slow","en-US",0,0.5}).timings.key);
        for(double invalid:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
            bool rejected=false; try{tts.Prepare("voice",config,{"hello","en-US",0,invalid});}catch(const std::invalid_argument&){rejected=true;} REQUIRE(rejected);
        }
        bool mismatch=false; try { tts.Prepare("voice",config,{"hello","zh-CN",0,1}); } catch(const std::runtime_error&){mismatch=true;} REQUIRE(mismatch);
#ifdef ADAYO_HAS_JSON_CONFIG
        AudioCache second(root/"tts-v1",{}); REQUIRE(!second.Stats().disk_enabled); REQUIRE(!second.Stats().warning.empty());
#endif
    }
#ifdef ADAYO_HAS_JSON_CONFIG
    {
        TtsService restarted; restarted.SetEngine(std::make_unique<FakeTtsEngine>()); restarted.InitializeCache(root/"tts-v1",{});
        auto hit=restarted.Prepare("voice",config,{"hello","en-US",0,1});
        std::cout << "restart source=" << hit.timings.source << " warning=" << restarted.Cache()->Stats().warning << " root=" << PathToUtf8(root) << '\n';
        REQUIRE(hit.timings.source=="disk"); REQUIRE(hit.timings.load_call_delta==0); REQUIRE(hit.timings.synth_call_delta==0); REQUIRE(hit.audio->samples==samples);
    }
    WriteBinaryFile(root/"tts-v1"/(key+".wav"),"bad",3);
    {
        TtsService rebuilt; rebuilt.SetEngine(std::make_unique<FakeTtsEngine>()); rebuilt.InitializeCache(root/"tts-v1",{});
        REQUIRE(rebuilt.Prepare("voice",config,{"hello","en-US",0,1}).timings.source=="synth");
        WriteBinaryFile(root/"model.onnx","new model content",17);
        auto changed=rebuilt.Prepare("voice",config,{"hello","en-US",0,1});
        REQUIRE(changed.timings.key!=key); REQUIRE(changed.timings.load_call_delta==1);
        config.num_threads=3;
        REQUIRE(rebuilt.Prepare("voice",config,{"hello","en-US",0,1}).timings.load_call_delta==1);
        const auto before_normalization=rebuilt.Prepare("voice",config,{"hello","en-US",0,1});
        config.text_normalization="nfd";
        const auto normalized=rebuilt.Prepare("voice",config,{"hello","en-US",0,1});
        REQUIRE(normalized.timings.key!=before_normalization.timings.key);
        REQUIRE(normalized.timings.load_call_delta==1);
        std::filesystem::remove(root/"model.onnx");
        bool missing=false; try{rebuilt.Prepare("voice",config,{"hello","en-US",0,1});}catch(const std::runtime_error&){missing=true;} REQUIRE(missing);
    }
#endif
}
void TestCacheEpochAndCancelableConcurrentMiss() {
    const auto root=NewCacheTestRoot(); auto config=CacheTestConfig(root);
    auto* engine=new FakeTtsEngine(); engine->block_synthesis=true;
    TtsService tts; tts.SetEngine(std::unique_ptr<ITtsEngine>(engine)); tts.InitializeCache(root/"tts-v1",{});
    PreparedAudio first,second; std::exception_ptr failure;
    std::jthread producer([&]{try{first=tts.Prepare("voice",config,{"hello","en-US",0,1});}catch(...){failure=std::current_exception();}});
    REQUIRE(engine->WaitForSynthStart(std::chrono::seconds{2}));
    std::stop_source cancel; cancel.request_stop(); bool rejected=false;
    try{tts.Prepare("voice",config,{"hello","en-US",0,1},cancel.get_token());}catch(const std::runtime_error&){rejected=true;} REQUIRE(rejected);
    tts.Cache()->BeginClear(); tts.Cache()->FinishClear(); engine->ReleaseSynthesis(); producer.join();
    if(failure) std::rethrow_exception(failure);
    REQUIRE(first.audio); REQUIRE(tts.Cache()->Stats().entries==0);
    std::jthread one([&]{first=tts.Prepare("voice",config,{"same","en-US",0,1});});
    std::jthread two([&]{second=tts.Prepare("voice",config,{"same","en-US",0,1});});
    one.join(); two.join();
    REQUIRE(first.timings.synth_call_delta+second.timings.synth_call_delta==1);
    REQUIRE(first.audio->samples==second.audio->samples);
    tts.Cache()->BeginClear(); const auto cleared=tts.Cache()->FinishClear(); REQUIRE(cleared.entries==0); REQUIRE(cleared.failed_entries==0);
    REQUIRE(!first.audio->samples.empty());
}

void TestCacheCapacityAndLru() {
    const auto root=NewCacheTestRoot();
    AudioCacheOptions options; options.entry_limit=2; options.memory_limit_bytes=640;
    AudioCache cache(root/"tts-v1",options);
    auto audio=std::make_shared<AudioBuffer>(); audio->sample_rate=16000; audio->channels=1; audio->samples.assign(160,0.1f);
    const auto a=Sha256("a"),b=Sha256("b"),c=Sha256("c");
    cache.Put(a,"voice",audio,0); cache.Put(b,"voice",audio,0);
    auto held=cache.Get(b,"voice",0); REQUIRE(held.audio);
    cache.Put(c,"voice",audio,0);
    REQUIRE(cache.Stats().entries==2); REQUIRE(cache.Stats().skipped>0);
    held.audio.reset(); audio.reset();
    auto fresh=std::make_shared<AudioBuffer>(); fresh->sample_rate=16000; fresh->samples.assign(160,0.2f);
    cache.Put(c,"voice",fresh,0); REQUIRE(cache.Stats().entries==2); REQUIRE(cache.Stats().evicted>=1);
    REQUIRE(cache.Stats().memory_bytes<=640); REQUIRE(cache.Stats().used_bytes<=options.disk_limit_bytes);
    auto held2=cache.Get(c,"voice",0); REQUIRE(held2.audio);
#ifdef ADAYO_HAS_JSON_CONFIG
    auto small_limits=options; small_limits.disk_limit_bytes=32;
    const auto oldLimit=cache.Stats().active_limit;
    bool rejected=false; try{cache.Configure(small_limits);}catch(const std::runtime_error&){rejected=true;}
    REQUIRE(rejected); REQUIRE(cache.Stats().active_limit==oldLimit);
#endif
    auto disabled=options; disabled.enabled=false; cache.Configure(disabled);
    const auto entries=cache.Stats().entries;
    REQUIRE(!cache.Get(c,"voice",0).audio); cache.Put(a,"voice",fresh,0); REQUIRE(cache.Stats().entries==entries);
}

void TestCacheFilesystemFailures() {
#if defined(ADAYO_HAS_JSON_CONFIG) && defined(_WIN32)
    const auto root=NewCacheTestRoot();
    const auto directory=root/"tts-v1";
    auto config=CacheTestConfig(root);
    const auto model_hash=FileSha256(root/"model.onnx");
    std::string key;
    {
        TtsService service;service.SetEngine(std::make_unique<FakeTtsEngine>());service.InitializeCache(directory,{});
        const auto result=service.Prepare("voice",config,{"fault","en-US",0,1});
        key=result.timings.key;
        const auto wav=directory/(key+".wav");
        const auto locked=CreateFileW(wav.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        REQUIRE(locked!=INVALID_HANDLE_VALUE);
        service.Cache()->BeginClear();
        const auto partial=service.Cache()->FinishClear();
        CloseHandle(locked);
        REQUIRE(partial.failed_entries==1); REQUIRE(partial.used_bytes>0); REQUIRE(partial.entries>0);
        REQUIRE(!partial.warning.empty()); REQUIRE(result.audio->samples.size()==160);
        service.Cache()->BeginClear();
        const auto clear=service.Cache()->FinishClear();
        REQUIRE(clear.failed_entries==0); REQUIRE(clear.entries==0);
        auto audio=std::make_shared<AudioBuffer>();audio->sample_rate=16000;audio->samples.assign(160,0.1f);
        const auto fail_key=Sha256("read-only-metadata");
        const auto metadata=directory/(fail_key+".json");
        WriteBinaryFile(metadata,"{}",2);
        REQUIRE(SetFileAttributesW(metadata.c_str(),FILE_ATTRIBUTE_READONLY));
        service.Cache()->Put(fail_key,"voice",audio,clear.epoch);
        const auto failed=service.Cache()->Stats();
        REQUIRE(SetFileAttributesW(metadata.c_str(),FILE_ATTRIBUTE_NORMAL));
        REQUIRE(failed.skipped>0); REQUIRE(!failed.warning.empty()); REQUIRE(failed.used_bytes>2);
        service.Cache()->BeginClear(); REQUIRE(service.Cache()->FinishClear().failed_entries==0);
    }
    const auto orphan=directory/(key+".wav.tmp-123-456-789");
    const auto unknown=directory/"unrelated.tmp";
    WriteBinaryFile(orphan,"orphan",6);WriteBinaryFile(unknown,"KEEP",4);
    {
        AudioCache cache(directory,{});
        REQUIRE(cache.Stats().disk_enabled); REQUIRE(!std::filesystem::exists(orphan)); REQUIRE(std::filesystem::exists(unknown));
    }
    const auto foreign=root/"foreign";std::filesystem::create_directory(foreign);WriteBinaryFile(foreign/"sentinel","KEEP",4);
    { AudioCache cache(foreign,{}); REQUIRE(!cache.Stats().disk_enabled); cache.BeginClear();cache.FinishClear(); }
    REQUIRE(FileSha256(foreign/"sentinel")==Sha256("KEEP"));
    const auto link=root/"linked-cache";
    if(CreateSymbolicLinkW(link.c_str(),foreign.c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY|SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
        AudioCache cache(link,{}); REQUIRE(!cache.Stats().disk_enabled); REQUIRE(!cache.Stats().warning.empty());
        cache.BeginClear();cache.FinishClear();
        REQUIRE(FileSha256(foreign/"sentinel")==Sha256("KEEP"));
    } else if(const auto* environment=std::getenv("ADAYO_REVIEW_CACHE_JUNCTION")) {
        const auto junction=PathFromUtf8(environment);
        const auto attributes=GetFileAttributesW(junction.c_str());
        REQUIRE(attributes!=INVALID_FILE_ATTRIBUTES); REQUIRE(attributes&FILE_ATTRIBUTE_REPARSE_POINT);
        AudioCache cache(junction,{}); REQUIRE(!cache.Stats().disk_enabled); REQUIRE(!cache.Stats().warning.empty());
        cache.BeginClear();cache.FinishClear();
        REQUIRE(FileSha256(junction/"sentinel")==Sha256("KEEP"));
        std::cout<<"PASS: native cache rejects directory junction without modifying its target\n";
    } else std::cout<<"BLOCKED: reparse injection requires symlink privilege or isolated junction fixture, error="<<GetLastError()<<"\n";
    REQUIRE(FileSha256(root/"model.onnx")==model_hash);
    {
        TtsService tts; auto* engine=new FakeTtsEngine();tts.SetEngine(std::unique_ptr<ITtsEngine>(engine));
        tts.InitializeCache(root/"identity",{});const TtsRequest request{"identity","en-US",0,1};
        auto previous=tts.Prepare("voice",config,request).timings.key;
        WriteBinaryFile(root/"tokens.txt","changed tokens",14);
        auto next=tts.Prepare("voice",config,request).timings.key;REQUIRE(next!=previous);previous=next;
        engine->runtime_identity="fake-runtime-v2";
        next=tts.Prepare("voice",config,request).timings.key;REQUIRE(next!=previous);
        auto limits=AudioCacheOptions{};limits.disk_limit_bytes=8192;
        AudioCache tiny(root/"tiny",limits);
        auto audio=std::make_shared<AudioBuffer>();audio->sample_rate=16000;audio->samples.assign(16000,0.2f);
        tiny.Put(Sha256("large"),"voice",audio,0); REQUIRE(tiny.Stats().skipped==1); REQUIRE(tiny.Stats().entries==0);
    }
#endif
}

class HandoffPlayer final : public IAudioPlayer {
public:
    void Play(const AudioBuffer&) override { throw std::logic_error("Context required"); }
    void Play(const AudioBuffer&, const std::shared_ptr<AudioPlaybackContext>& context) override {
        {
            std::unique_lock lock(mutex);
            arrived = true; cv.notify_all();
            cv.wait(lock, [&] { return released; });
        }
        std::unique_lock lock(context->mutex);
        observed_pause = context->paused;
        observed_cancel = context->canceled;
        inspected = true; cv.notify_all();
        context->cv.wait(lock, [&] { return context->canceled || !context->paused; });
        if (!context->canceled) ++starts;
    }
    void Pause() override {}
    void Resume() override {}
    void Stop() override {}
    void WaitArrival() {
        std::unique_lock lock(mutex);
        REQUIRE(cv.wait_for(lock, std::chrono::seconds{2}, [&] { return arrived; }));
    }
    void Release() { std::lock_guard lock(mutex); released = true; cv.notify_all(); }
    std::mutex mutex;
    std::condition_variable cv;
    bool arrived{}, released{};
    std::atomic<bool> inspected{}, observed_pause{}, observed_cancel{};
    std::atomic<int> starts{};
};

void TestControlsAtDeviceHandoff() {
    for (bool pause : {false, true}) {
        TtsService tts;
        tts.SetEngine(std::make_unique<FakeTtsEngine>());
        HandoffPlayer player;
        PlaybackService playback(tts, player);
        playback.Play(MakePlaybackRequest({{{"handoff", "en-US", 0, 1.0}, 1, 1}}));
        player.WaitArrival();
        if (pause) playback.Pause(); else playback.Stop();
        player.Release();
        {
            std::unique_lock lock(player.mutex);
            REQUIRE(player.cv.wait_for(lock, std::chrono::seconds{2}, [&] { return player.inspected.load(); }));
        }
        REQUIRE(player.starts == 0);
        if (pause) { REQUIRE(player.observed_pause); REQUIRE(playback.State() == PlaybackState::Paused); playback.Resume(); }
        else REQUIRE(player.observed_cancel);
        REQUIRE(playback.WaitUntilIdle(std::chrono::seconds{2}));
        REQUIRE(player.starts == (pause ? 1 : 0));
    }
}

void TestWorkerQueueDiscardPending() {
    WorkerQueue queue;
    std::mutex mutex;
    std::condition_variable cv;
    bool first_started = false;
    int executed = 0;

    queue.Submit([&](std::stop_token stop) {
        {
            std::lock_guard lock(mutex);
            first_started = true;
            ++executed;
        }
        cv.notify_all();
        std::unique_lock lock(mutex);
        std::stop_callback stopped(stop,[&] { cv.notify_all(); });
        cv.wait(lock, [&] { return stop.stop_requested(); });
    });
    queue.Submit([&](std::stop_token) { ++executed; });

    {
        std::unique_lock lock(mutex);
        REQUIRE(cv.wait_for(lock, std::chrono::seconds{2}, [&] { return first_started; }));
    }
    std::jthread stopper([&] {
        queue.Stop(StopMode::DiscardPending);
    });
    stopper.join();
    REQUIRE(executed == 1);
}

void TestWorkerQueueStopIsIdempotentAndConstructionIsStable() {
    for (int i = 0; i < 10000; ++i) {
        WorkerQueue queue;
        queue.Submit([](std::stop_token) {});
        queue.Stop(i % 2 == 0 ? StopMode::Drain : StopMode::DiscardPending);
        queue.Stop(StopMode::DiscardPending);
        queue.Stop(StopMode::Drain);
    }
}

void TestWorkerQueueUnhandledTaskErrorDoesNotTerminateWorker() {
    std::mutex mutex;
    std::condition_variable cv;
    int errors = 0;
    int after = 0;
    WorkerQueue queue([&](std::exception_ptr error) {
        try {
            if (error) std::rethrow_exception(error);
        } catch (const std::runtime_error& ex) {
            REQUIRE(std::string(ex.what()) == "probe");
            std::lock_guard lock(mutex);
            ++errors;
        }
        cv.notify_all();
    });

    queue.Submit([](std::stop_token) { throw std::runtime_error("probe"); });
    queue.Submit([&](std::stop_token) {
        {
            std::lock_guard lock(mutex);
            ++after;
        }
        cv.notify_all();
    });

    std::unique_lock lock(mutex);
    REQUIRE(cv.wait_for(lock, std::chrono::seconds{2}, [&] { return errors == 1 && after == 1; }));
    lock.unlock();
    queue.Stop();
}
} // namespace

int main() {
    return test::RunTestMain("adayo_p5_playback_tests", [] {
        TestSequenceSkipsEmptyAndReturnsIdle();
        TestPauseResumeAndStopDoNotLeaveStaleState();
        TestPauseDuringGeneratingWaitsBeforePlaying();
        TestStopDuringIntervalExitsPromptly();
        TestStopThenNewSequenceIgnoresOldWorkerState();
        TestSameModelIsLoadedOnce();
        TestModelSwitchLoadsOnlyOnVoiceChange();
        TestEngineMismatchRejectedAtExecutionLayer();
        TestLoadFailureClearsActiveModelId();
        TestStalePlaybackRequestDoesNotLoadOldModel();
        TestRepeatedStopDuringSynthesis();
        TestControlsAtDeviceHandoff();
        TestCacheServiceIdentityAndRestart();
        TestCacheEpochAndCancelableConcurrentMiss();
        TestCacheCapacityAndLru();
        TestCacheFilesystemFailures();
        TestWorkerQueueDiscardPending();
        TestWorkerQueueStopIsIdempotentAndConstructionIsStable();
        TestWorkerQueueUnhandledTaskErrorDoesNotTerminateWorker();
    });
}
