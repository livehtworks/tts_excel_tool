#include "adapters/audio/IAudioPlayer.h"
#include "core/tts/ITtsEngine.h"
#include "services/PlaybackService.h"

#include "TestCheck.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace adayo;

namespace {
class FakeTtsEngine final : public ITtsEngine {
public:
    std::string Id() const override { return "fake"; }
    bool IsLoaded() const noexcept override { return loaded_; }
    void Load(const TtsModelConfig&) override { loaded_ = true; }
    void Unload() noexcept override { loaded_ = false; }
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

void TestSequenceSkipsEmptyAndReturnsIdle() {
    auto* engine = new FakeTtsEngine();
    TtsService tts;
    tts.SetEngine(std::unique_ptr<ITtsEngine>(engine));
    tts.LoadModel({});
    FakePlayer player;
    PlaybackService playback(tts, player);

    playback.PlaySequence({
        {{"first", "en-US", 0, 1.0}, 10, 2},
        {{"", "en-US", 0, 1.0}, 11, 2},
        {{"third", "en-US", 0, 1.0}, 12, 2},
    }, std::chrono::milliseconds{1}, 1.0);

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
    tts.LoadModel({});
    FakePlayer player;
    PlaybackService playback(tts, player);

    playback.PlaySequence({{{"first", "en-US", 0, 1.0}, 1, 1}}, std::chrono::milliseconds{0}, 1.0);
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
    tts.LoadModel({});
    FakePlayer player;
    PlaybackService playback(tts, player);

    playback.PlaySequence({{{"slow", "en-US", 0, 1.0}, 7, 3}}, std::chrono::milliseconds{0}, 1.0);
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
    tts.LoadModel({});
    FakePlayer player;
    PlaybackService playback(tts, player);

    playback.PlaySequence({
        {{"first", "en-US", 0, 1.0}, 1, 1},
        {{"second", "en-US", 0, 1.0}, 2, 1},
    }, std::chrono::seconds{3}, 1.0);
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
    tts.LoadModel({});
    FakePlayer player;
    PlaybackService playback(tts, player);

    playback.PlaySequence({{{"old", "en-US", 0, 1.0}, 1, 1}}, std::chrono::milliseconds{0}, 1.0);
    REQUIRE(engine->WaitForSynthStart(std::chrono::seconds{2}));
    playback.Stop();
    playback.PlaySequence({{{"new", "en-US", 0, 1.0}, 2, 1}}, std::chrono::milliseconds{0}, 1.0);
    engine->ReleaseSynthesis();
    REQUIRE(playback.WaitUntilIdle(std::chrono::seconds{2}));
    REQUIRE(player.play_count == 1);
    REQUIRE(playback.CurrentRow() == 2);
}
} // namespace

int main() {
    return test::RunTestMain("adayo_p5_playback_tests", [] {
        TestSequenceSkipsEmptyAndReturnsIdle();
        TestPauseResumeAndStopDoNotLeaveStaleState();
        TestPauseDuringGeneratingWaitsBeforePlaying();
        TestStopDuringIntervalExitsPromptly();
        TestStopThenNewSequenceIgnoresOldWorkerState();
    });
}
