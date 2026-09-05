#pragma once

#include "core/tts/ITtsEngine.h"

#include <memory>
#include <mutex>
#include <string_view>

namespace adayo {

class TtsService {
public:
    void SetEngine(std::unique_ptr<ITtsEngine> engine);
    void EnsureModelLoaded(std::string_view model_id, const TtsModelConfig& config);
    AudioBuffer Synthesize(const TtsRequest& request);
    void Unload() noexcept;
    std::string ActiveEngineId() const;
    std::string ActiveModelId() const;

private:
    mutable std::mutex mutex_;
    std::unique_ptr<ITtsEngine> engine_;
    std::string active_model_id_;
};

} // namespace adayo
