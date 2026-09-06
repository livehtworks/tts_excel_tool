#include "adapters/audio/IAudioPlayer.h"
#include "core/tts/ITtsEngine.h"
#include "services/PlaybackService.h"
#include "platform/FileIo.h"
#include "platform/UnicodePath.h"
#include <filesystem>
#include <limits>
#include <bit>

#include "TestCheck.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <future>
#include <fstream>
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
        if(warm.timings.source!="memory") std::cerr<<"Unexpected cache miss: "<<tts.Cache()->Stats().warning<<'\n';
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
    REQUIRE(cache.Root()==std::filesystem::absolute(root/"tts-v1").lexically_normal());
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

void TestDiskHitSurvivesLruWriteFailure() {
#if defined(ADAYO_HAS_JSON_CONFIG) && defined(_WIN32)
    const auto root=NewCacheTestRoot();
    const auto config=CacheTestConfig(root);
    const TtsRequest request{"LRU write failure", "en-US",0,1};
    std::string key;
    std::vector<float> expected;
    {
        TtsService service; service.SetEngine(std::make_unique<FakeTtsEngine>());
        service.InitializeCache(root/"tts-v1",{});
        const auto result=service.Prepare("voice",config,request);
        key=result.timings.key; expected=result.audio->samples;
    }
    TtsService service; service.SetEngine(std::make_unique<FakeTtsEngine>());
    service.InitializeCache(root/"tts-v1",{});
    const auto metadata=root/"tts-v1"/(key+".json");
    const auto handle=CreateFileW(metadata.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    REQUIRE(handle!=INVALID_HANDLE_VALUE);
    const auto close=[](void* value){CloseHandle(value);};
    std::unique_ptr<void,decltype(close)> held(handle,close);
    const auto result=service.Prepare("voice",config,request);
    service.Cache()->FlushUsage();
    std::cout << "CACHE01 source=" << result.timings.source << " load=" << result.timings.load_call_delta
              << " synth=" << result.timings.synth_call_delta << " warning=" << service.Cache()->Stats().warning << '\n';
    REQUIRE(result.timings.source=="disk");
    REQUIRE(result.timings.load_call_delta==0); REQUIRE(result.timings.synth_call_delta==0);
    REQUIRE(result.audio->samples==expected);
    REQUIRE(std::filesystem::exists(root/"tts-v1"/(key+".wav")));
    REQUIRE(!service.Cache()->Stats().warning.empty());
    REQUIRE(service.Cache()->Stats().timestamp_write_failures==1);
    service.Cache()->FlushUsage();
    REQUIRE(service.Cache()->Stats().timestamp_write_failures==1);
#endif
}

void TestSharedAssetsAndStableV1Keys() {
    const auto root=NewCacheTestRoot(); auto config=CacheTestConfig(root);
    TtsService tts; tts.SetEngine(std::make_unique<FakeTtsEngine>());
    AudioCacheOptions options; options.enabled=false; tts.InitializeCache(root/"tts-v1",options);
    const auto field=[](std::string& out,const std::string& value) { out+=std::to_string(value.size())+":"+value; };
    for(int speaker=0;speaker<130;++speaker) {
        config.speaker_id=speaker; const auto id="voice-"+std::to_string(speaker);
        const auto result=tts.Prepare(id,config,{"same text","en-US",speaker,1});
        REQUIRE(result.timings.load_call_delta==(speaker==0?1:0));
        REQUIRE(result.timings.synth_call_delta==1);
        REQUIRE(result.timings.resource_hash_bytes==(speaker==0?11:0));
        REQUIRE(tts.ActiveModelId()==id);
        std::string fingerprint;
        for(const auto& value:{config.engine_id,std::string("fake-runtime-v1"),config.model_path,config.tokens_path,
            config.data_dir,config.lexicon_path,config.rule_fsts,config.language_code,std::to_string(speaker),std::to_string(config.num_threads),config.text_normalization}) field(fingerprint,value);
        field(fingerprint,Sha256("model")); field(fingerprint,Sha256("tokens"));
        std::string expected;
        for(const auto& value:{std::string("tts-cache-v1"),id,Sha256(fingerprint),std::string("same text"),config.language_code,
            std::to_string(speaker),std::to_string(std::bit_cast<std::uint32_t>(1.0f))}) field(expected,value);
        REQUIRE(result.timings.key==Sha256(expected));
    }
    config.speaker_id=0;
    REQUIRE(tts.Prepare("voice-0",config,{"back","en-US",0,1}).timings.load_call_delta==0);
    WriteBinaryFile(root/"tokens.txt","new tokens",10);
    const auto changed=tts.Prepare("voice-0",config,{"back","en-US",0,1});
    REQUIRE(changed.timings.resource_hash_bytes==10); REQUIRE(changed.timings.load_call_delta==1);
}

void TestUsageBatchingAndSharedReferences() {
    const auto root=NewCacheTestRoot();
    AudioCache cache(root/"tts-v1",{});
    auto audio=std::make_shared<AudioBuffer>(); audio->sample_rate=16000; audio->samples.assign(160,0.1f);
    const auto a=Sha256("shared-a"),b=Sha256("shared-b");
    cache.Put(a,"voice",audio,0); cache.Put(b,"voice",audio,0);
    REQUIRE(cache.Stats().memory_bytes==640); REQUIRE(cache.Stats(true).active_bytes==640);
    const auto before=cache.Stats();
    for(int i=0;i<31;++i) REQUIRE(cache.Get(a,"voice",0).source=="memory");
    REQUIRE(cache.Stats().metadata_writes==before.metadata_writes);
    REQUIRE(cache.Stats().recounts==before.recounts);
    REQUIRE(cache.Get(a,"voice",0).source=="memory");
#if defined(ADAYO_HAS_JSON_CONFIG) && defined(_WIN32)
    REQUIRE(cache.Stats().metadata_writes==before.metadata_writes+1);
    REQUIRE(cache.Get(b,"voice",0).source=="memory");
    REQUIRE(cache.Stats().dirty_entries==1);
    cache.FlushUsage(); REQUIRE(cache.Stats().dirty_entries==0);
    REQUIRE(cache.Stats().metadata_writes==before.metadata_writes+2);
#endif
    audio.reset(); REQUIRE(cache.Stats(true).active_bytes==0);
    auto active=cache.Get(a,"voice",0);
    cache.BeginClear(); cache.FinishClear();
    REQUIRE(cache.Stats().memory_bytes==0); REQUIRE(cache.Stats(true).active_bytes==640);
    REQUIRE(active.audio->samples.size()==160);
}

void TestHitFlushBudgetAndDrain() {
#if defined(ADAYO_HAS_JSON_CONFIG) && defined(_WIN32)
    AudioCache cache(NewCacheTestRoot()/"tts-v1",{});
    auto audio=std::make_shared<AudioBuffer>();
    audio->sample_rate=16000; audio->samples.assign(160,0.1f);
    std::vector<std::string> keys;
    for(int i=0;i<32;++i) {
        keys.push_back(Sha256("flush-budget-"+std::to_string(i)));
        cache.Put(keys.back(),"voice",audio,0);
    }
    const auto before=cache.Stats();
    for(const auto& key:keys) REQUIRE(cache.Get(key,"voice",0).source=="memory");
    REQUIRE(cache.Stats().metadata_writes==before.metadata_writes+2);
    REQUIRE(cache.Stats().dirty_entries==30);
    REQUIRE(cache.Stats().recounts==before.recounts);
    cache.FlushUsage();
    REQUIRE(cache.Stats().dirty_entries==0);
    REQUIRE(cache.Stats().metadata_writes==before.metadata_writes+32);
    cache.FlushUsage();
    REQUIRE(cache.Stats().metadata_writes==before.metadata_writes+32);
#endif
}

void TestCacheScale(std::size_t count) {
    REQUIRE(count<=20000);
    const auto root=NewCacheTestRoot()/"tts-v1";
    AudioCacheOptions options; options.entry_limit=(std::max)(count,std::size_t{1});
    AudioCache cache(root,options);
    const auto put=[&](const std::string& key) {
        auto audio=std::make_shared<AudioBuffer>(); audio->sample_rate=16000; audio->samples.assign(16,0.2f);
        cache.Put(key,"scale",std::move(audio),cache.Stats().epoch);
    };
    const auto snapshot=[&](const char* phase) {
        const auto stats=cache.Stats(true);
        std::uint64_t actual=0;
        for(const auto& file:std::filesystem::directory_iterator(root)) if(file.is_regular_file()) actual+=file.file_size();
        REQUIRE(stats.accounting_valid); REQUIRE(stats.used_bytes==actual); REQUIRE(actual<=stats.active_limit);
        std::cout<<"SCALE count="<<count<<",phase="<<phase<<",entries="<<stats.entries<<",bytes="<<actual
            <<",recounts="<<stats.recounts<<",metadata_writes="<<stats.metadata_writes<<",eviction_scans="<<stats.eviction_scans
            <<",pin_checks="<<stats.pin_scans<<",memory_bytes="<<stats.memory_bytes<<",active_bytes="<<stats.active_bytes<<'\n';
    };
    for(std::size_t i=0;i<count;++i) put(Sha256("scale-"+std::to_string(i)));
    snapshot("seed"); REQUIRE(cache.Stats().entries==count);
    const auto before=cache.Stats();
    for(std::size_t i=0;i<50;++i) {
        const auto hit=cache.Get(Sha256("scale-"+std::to_string(count?i%count:0)),"scale",0);
        REQUIRE(static_cast<bool>(hit.audio)==(count>0));
    }
    REQUIRE(cache.Stats().recounts==before.recounts); snapshot("hits");
    for(std::size_t i=0;i<50;++i) put(Sha256("new-"+std::to_string(i)));
    snapshot("put-evict");
    options.entry_limit=(std::max)(std::size_t{1},count/2);
    cache.Configure(options); REQUIRE(cache.Stats().entries<=options.entry_limit); snapshot("shrink");
    cache.FlushUsage(); REQUIRE(cache.Stats().dirty_entries==0); snapshot("flush");
    cache.BeginClear(); const auto cleared=cache.FinishClear();
    REQUIRE(cleared.failed_entries==0); REQUIRE(cleared.entries==0); snapshot("clear");
}

void TestPartialClearAccounting() {
#if defined(ADAYO_HAS_JSON_CONFIG) && defined(_WIN32)
    const auto root=NewCacheTestRoot()/"tts-v1";
    AudioCacheOptions options; options.disk_limit_bytes=32768;
    AudioCache cache(root,options);
    auto audio=std::make_shared<AudioBuffer>(); audio->sample_rate=16000; audio->samples.assign(160,0.2f);
    std::vector<HANDLE> locks;
    for(int i=0;i<10;++i) {
        const auto key=Sha256("partial-"+std::to_string(i));
        cache.Put(key,"voice",audio,0);
        if(i%2==0) {
            const auto handle=CreateFileW((root/(key+".wav")).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            REQUIRE(handle!=INVALID_HANDLE_VALUE); locks.push_back(handle);
        }
    }
    REQUIRE(cache.Stats().entries==10);
    WriteBinaryFile(root/"foreign.tmp","KEEP",4);
    const auto owner_hash=FileSha256(root/"owner.json");
    cache.BeginClear(); const auto partial=cache.FinishClear();
    for(const auto handle:locks) CloseHandle(handle);
    REQUIRE(partial.failed_entries==5); REQUIRE(partial.entries==5);
    REQUIRE(!partial.warning.empty()); REQUIRE(partial.accounting_valid);
    std::uint64_t actual=0;
    for(const auto& file:std::filesystem::directory_iterator(root)) if(file.is_regular_file()) actual+=file.file_size();
    REQUIRE(partial.used_bytes==actual); REQUIRE(actual<=partial.active_limit);
    REQUIRE(cache.Stats(true).active_bytes==640); REQUIRE(audio->samples.front()==0.2f);
    cache.BeginClear(); const auto complete=cache.FinishClear();
    REQUIRE(complete.failed_entries==0); REQUIRE(complete.entries==0);
    REQUIRE(FileSha256(root/"foreign.tmp")==Sha256("KEEP")); REQUIRE(FileSha256(root/"owner.json")==owner_hash);
    std::cout<<"PASS: half of 10 entries fail deletion, exact bytes/quota and live PCM lease preserved\n";
#endif
}

void SeedAbnormalCacheExit() {
#if defined(ADAYO_HAS_JSON_CONFIG) && defined(_WIN32)
    const auto root=std::filesystem::current_path();
    REQUIRE(FileSha256(root/"disposable-fixture")==Sha256("R2 crash test"));
    AudioCache cache(root/"tts-v1",{});
    auto audio=std::make_shared<AudioBuffer>(); audio->sample_rate=16000; audio->samples.assign(160,0.2f);
    const auto key=Sha256("abnormal-cache");
    cache.Put(key,"voice",audio,0);
    const auto hash=FileSha256(root/"tts-v1"/(key+".json"));
    WriteBinaryFile(root/"metadata-before-hit",hash.data(),hash.size());
    REQUIRE(cache.Get(key,"voice",0).source=="memory"); REQUIRE(cache.Stats().dirty_entries==1);
    // Intentionally bypass destructors in this isolated child, as a process crash would.
    std::_Exit(23);
#endif
}

void TestCacheAfterAbnormalExit() {
#if defined(ADAYO_HAS_JSON_CONFIG) && defined(_WIN32)
    const auto root=NewCacheTestRoot();
    WriteBinaryFile(root/"disposable-fixture","R2 crash test",13);
    std::wstring executable(32768,L'\0');
    const auto length=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
    REQUIRE(length>0 && length<executable.size()); executable.resize(length);
    auto command=L"\""+executable+L"\" --cache-abnormal-seed";
    STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION process{};
    REQUIRE(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,root.c_str(),&startup,&process));
    const auto waited=WaitForSingleObject(process.hProcess,30000);
    DWORD code=0; const auto read_code=GetExitCodeProcess(process.hProcess,&code);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    REQUIRE(waited==WAIT_OBJECT_0); REQUIRE(read_code); REQUIRE(code==23);
    const auto key=Sha256("abnormal-cache");
    const auto directory=root/"tts-v1";
    std::ifstream saved(root/"metadata-before-hit"); std::string before; saved>>before;
    REQUIRE(FileSha256(directory/(key+".json"))==before);
    const auto owner=FileSha256(directory/"owner.json");
    AudioCache cache(directory,{}); REQUIRE(cache.Stats().disk_enabled);
    const auto hit=cache.Get(key,"voice",0);
    REQUIRE(hit.source=="disk"); REQUIRE(hit.audio && hit.audio->samples.size()==160 && hit.audio->samples.front()==0.2f);
    cache.FlushUsage(); REQUIRE(cache.Stats().dirty_entries==0);
    REQUIRE(FileSha256(directory/(key+".json"))!=before);
    REQUIRE(FileSha256(directory/"owner.json")==owner);
    std::cout<<"PASS: child exit=23 without drain; validated PCM and ownership survive, normal drain persists usage\n";
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

void TestNonblockingStopAndPlaybackSnapshot() {
    WorkerQueue queue;
    std::promise<void> entered,release;
    auto released=release.get_future().share();
    std::atomic<bool> returned{},pending_ran{};
    queue.Submit([&](std::stop_token) { entered.set_value(); released.wait(); returned=true; });
    entered.get_future().get();
    queue.Submit([&](std::stop_token) { pending_ran=true; });
    queue.RequestStop(StopMode::DiscardPending);
    const bool stopped_before_release=!returned;
    bool rejected=false;
    try { queue.Submit([](std::stop_token) {}); } catch(const std::exception&) { rejected=true; }
    release.set_value(); queue.Stop();
    REQUIRE(stopped_before_release); REQUIRE(rejected); REQUIRE(returned); REQUIRE(!pending_ran);

    auto* engine=new FakeTtsEngine(); engine->block_synthesis=true;
    TtsService tts; tts.SetEngine(std::unique_ptr<ITtsEngine>(engine));
    FakePlayer player; PlaybackService playback(tts,player);
    playback.Play(MakePlaybackRequest({{{"first","en-US",0,1.0},41,7}}));
    const bool entered_synth=engine->WaitForSynthStart(std::chrono::seconds(2));
    const auto first=playback.Snapshot();
    playback.Stop();
    const auto stopping=playback.Snapshot();
    playback.Play(MakePlaybackRequest({{{"second","en-US",0,1.0},3,9}}));
    const auto second=playback.Snapshot();
    playback.RequestShutdown();
    const auto shutdown=playback.Snapshot();
    playback.Play(MakePlaybackRequest({{{"rejected","en-US",0,1.0},99,99}}));
    const auto rejected_play=playback.Snapshot();
    engine->ReleaseSynthesis(); playback.Shutdown();
    REQUIRE(entered_synth);
    REQUIRE(first.state==PlaybackState::Generating && first.row==41 && first.column==7 && first.request_id!=0);
    REQUIRE(stopping.state==PlaybackState::Stopping && stopping.request_id==first.request_id);
    REQUIRE(second.state==PlaybackState::Generating && second.row==3 && second.column==9);
    REQUIRE(second.request_id>first.request_id);
    REQUIRE(shutdown.state==PlaybackState::Stopping);
    REQUIRE(rejected_play.request_id==shutdown.request_id && rejected_play.row==3);
    REQUIRE(playback.Snapshot().state==PlaybackState::Idle);
    REQUIRE(playback.Snapshot().request_id==0);
    REQUIRE(player.play_count==0);
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

int main(int argc,char** argv) {
    if(argc==2 && std::string(argv[1])=="--cache-abnormal-seed") {
        SeedAbnormalCacheExit(); return 1;
    }
    if(argc==3 && std::string(argv[1])=="--r2-cache-scale")
        return test::RunTestMain("R2_CACHE_SCALE",[&] { TestCacheScale(std::stoull(argv[2])); });
    if(argc==2 && std::string(argv[1])=="--r2-cache01")
        return test::RunTestMain("R2_CACHE01",TestDiskHitSurvivesLruWriteFailure);
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
        TestDiskHitSurvivesLruWriteFailure();
        TestSharedAssetsAndStableV1Keys();
        TestUsageBatchingAndSharedReferences();
        TestHitFlushBudgetAndDrain();
        TestPartialClearAccounting();
        TestCacheAfterAbnormalExit();
        TestWorkerQueueDiscardPending();
        TestNonblockingStopAndPlaybackSnapshot();
        TestWorkerQueueStopIsIdempotentAndConstructionIsStable();
        TestWorkerQueueUnhandledTaskErrorDoesNotTerminateWorker();
    });
}
