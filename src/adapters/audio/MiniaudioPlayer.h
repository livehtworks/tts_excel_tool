#pragma once

#include "adapters/audio/IAudioPlayer.h"

#include <memory>

namespace adayo {
class MiniaudioPlayer final : public IAudioPlayer {
public:
    struct Impl;

    MiniaudioPlayer();
    ~MiniaudioPlayer() override;
    void Play(const AudioBuffer& audio) override;
    void Pause() override;
    void Resume() override;
    void Stop() override;
private:
    std::unique_ptr<Impl> impl_;
};
} // namespace adayo
