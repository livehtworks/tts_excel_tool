#pragma once

#include "core/domain/Types.h"

namespace adayo {
class IAudioPlayer {
public:
    virtual ~IAudioPlayer() = default;
    virtual void Play(const AudioBuffer& audio) = 0;
    virtual void Pause() = 0;
    virtual void Resume() = 0;
    virtual void Stop() = 0;
};
} // namespace adayo
