#include "adapters/audio/IAudioPlayer.h"
#include "core/tts/ITtsEngine.h"
#include "services/PlaybackService.h"

#include "TestCheck.h"

#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace adayo;

namespace {
class FakeTtsEngine final : public ITtsEngine {
public:
    std::string Id() const override { return "fake"; }
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

    int synth_count{};
    int load_count{};
    int unload_count{};
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
    int play_count{};
    int stop_count{};
    int resume_count{};
    bool playing{false};
    bool paused{false};
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

    playback.Play(MakePlaybackRequest({{{"old", "en-US", 0, 1.0}, 1, 1}},
        std::chrono::milliseconds{0}, 1.0, "old"));
    playback.Play(MakePlaybackRequest({{{"new", "en-US", 0, 1.0}, 2, 1}},
        std::chrono::milliseconds{0}, 1.0, "new"));
    engine->ReleaseSynthesis();
    REQUIRE(playback.WaitUntilIdle(std::chrono::seconds{2}));
    REQUIRE(engine->load_history.size() == 1);
    REQUIRE(engine->load_history[0] == "new");
}

void TestWorkerQueueDiscardPending() {
    WorkerQueue queue;
    std::mutex mutex;
    std::condition_variable cv;
    bool first_started = false;
    bool release_first = false;
    int executed = 0;

    queue.Submit([&](std::stop_token) {
        {
            std::lock_guard lock(mutex);
            first_started = true;
            ++executed;
        }
        cv.notify_all();
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return release_first; });
    });
    queue.Submit([&](std::stop_token) { ++executed; });

    {
        std::unique_lock lock(mutex);
        REQUIRE(cv.wait_for(lock, std::chrono::seconds{2}, [&] { return first_started; }));
    }
    std::jthread stopper([&] {
        queue.Stop(StopMode::DiscardPending);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    {
        std::lock_guard lock(mutex);
        release_first = true;
    }
    cv.notify_all();
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
        TestWorkerQueueDiscardPending();
        TestWorkerQueueStopIsIdempotentAndConstructionIsStable();
        TestWorkerQueueUnhandledTaskErrorDoesNotTerminateWorker();
    });
}
