#include "services/TtsService.h"

#include <stdexcept>

namespace adayo {

void TtsService::SetEngine(std::unique_ptr<ITtsEngine> engine) {
    if (!engine) throw std::invalid_argument("TTS engine 不能为空");
    std::scoped_lock lock(mutex_);
    if (engine_) engine_->Unload();
    engine_ = std::move(engine);
}

void TtsService::LoadModel(const TtsModelConfig& config) {
    std::scoped_lock lock(mutex_);
    if (!engine_) throw std::runtime_error("尚未设置 TTS engine");
    engine_->Load(config);
}

AudioBuffer TtsService::Synthesize(const TtsRequest& request) {
    std::scoped_lock lock(mutex_);
    if (!engine_ || !engine_->IsLoaded()) throw std::runtime_error("TTS 模型尚未加载");
    return engine_->Synthesize(request);
}

void TtsService::Unload() noexcept {
    std::scoped_lock lock(mutex_);
    if (engine_) engine_->Unload();
}

std::string TtsService::ActiveEngineId() const {
    std::scoped_lock lock(mutex_);
    return engine_ ? engine_->Id() : std::string{};
}

} // namespace adayo
