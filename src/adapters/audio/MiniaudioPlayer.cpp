#include "adapters/audio/MiniaudioPlayer.h"

#include <stdexcept>

namespace adayo {
struct MiniaudioPlayer::Impl {};
MiniaudioPlayer::MiniaudioPlayer() : impl_(std::make_unique<Impl>()) {}
MiniaudioPlayer::~MiniaudioPlayer() = default;
void MiniaudioPlayer::Play(const AudioBuffer&) { throw std::runtime_error("P5: miniaudio callback/device lifecycle 尚待接线"); }
void MiniaudioPlayer::Pause() { }
void MiniaudioPlayer::Resume() { }
void MiniaudioPlayer::Stop() { }
} // namespace adayo
