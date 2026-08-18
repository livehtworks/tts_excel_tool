#include "adapters/audio/IAudioPlayer.h"
#include "core/tts/ITtsEngine.h"
#include "services/PlaybackService.h"

#include <cassert>
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
        ++synth_count;
        last_text = request.text;
        AudioBuffer audio;
        audio.sample_rate = 16000;
        audio.channels = 1;
        audio.samples.assign(160, 0.1f);
        return audio;
    }

    int synth_count{};
    std::string last_text;

private:
    bool loaded_{false};
};

class FakePlayer final : public IAudioPlayer {
public:
    void Play(const AudioBuffer& audio) override {
        assert(!audio.samples.empty());
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

    assert(playback.WaitUntilIdle(std::chrono::seconds{2}));
    assert(playback.State() == PlaybackState::Idle);
    assert(engine->synth_count == 2);
    assert(player.play_count == 2);
    assert(playback.CurrentRow() == 12);
}

void TestPauseResumeAndStopDoNotLeaveStaleState() {
    auto* engine = new FakeTtsEngine();
    TtsService tts;
    tts.SetEngine(std::unique_ptr<ITtsEngine>(engine));
    tts.LoadModel({});
    FakePlayer player;
    PlaybackService playback(tts, player);

    playback.PlaySequence({{{"first", "en-US", 0, 1.0}, 1, 1}}, std::chrono::milliseconds{0}, 1.0);
    assert(player.WaitForPlayCount(1, std::chrono::seconds{2}));
    playback.Pause();
    assert(playback.State() == PlaybackState::Paused || playback.State() == PlaybackState::Idle);
    playback.Resume();
    playback.Stop();
    assert(playback.WaitUntilIdle(std::chrono::seconds{2}));
    assert(playback.State() == PlaybackState::Idle);
    assert(player.stop_count >= 1);
    assert(player.resume_count >= 1);
}
} // namespace

int main() {
    TestSequenceSkipsEmptyAndReturnsIdle();
    TestPauseResumeAndStopDoNotLeaveStaleState();
    return 0;
}
