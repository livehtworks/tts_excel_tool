#include "services/TtsService.h"

#include <stdexcept>

namespace adayo {

void TtsService::SetEngine(std::unique_ptr<ITtsEngine> engine) {
    if (!engine) throw std::invalid_argument("TTS engine 不能为空");
    std::scoped_lock lock(mutex_);
    if (engine_) engine_->Unload();
    engine_ = std::move(engine);
    active_model_id_.clear();
}

void TtsService::EnsureModelLoaded(std::string_view model_id, const TtsModelConfig& config) {
    std::scoped_lock lock(mutex_);
    if (!engine_) throw std::runtime_error("尚未设置 TTS engine");
    if (config.engine_id != engine_->Id()) {
        throw std::runtime_error("ENGINE_MISMATCH: 模型 engine_id 与当前 TTS engine 不一致");
    }
    if (engine_->IsLoaded() && active_model_id_ == model_id) {
        return;
    }
    active_model_id_.clear();
    if (engine_->IsLoaded()) {
        engine_->Unload();
    }
    engine_->Load(config);
    active_model_id_ = std::string(model_id);
}

AudioBuffer TtsService::Synthesize(const TtsRequest& request) {
    std::scoped_lock lock(mutex_);
    if (!engine_ || !engine_->IsLoaded()) throw std::runtime_error("TTS 模型尚未加载");
    return engine_->Synthesize(request);
}

void TtsService::Unload() noexcept {
    std::scoped_lock lock(mutex_);
    if (engine_) engine_->Unload();
    active_model_id_.clear();
}

std::string TtsService::ActiveEngineId() const {
    std::scoped_lock lock(mutex_);
    return engine_ ? engine_->Id() : std::string{};
}

std::string TtsService::ActiveModelId() const {
    std::scoped_lock lock(mutex_);
    return active_model_id_;
}

} // namespace adayo
