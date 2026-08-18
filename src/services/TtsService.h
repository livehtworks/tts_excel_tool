#pragma once

#include "core/tts/ITtsEngine.h"

#include <memory>
#include <mutex>

namespace adayo {

class TtsService {
public:
    void SetEngine(std::unique_ptr<ITtsEngine> engine);
    void LoadModel(const TtsModelConfig& config);
    AudioBuffer Synthesize(const TtsRequest& request);
    void Unload() noexcept;
    std::string ActiveEngineId() const;

private:
    mutable std::mutex mutex_;
    std::unique_ptr<ITtsEngine> engine_;
};

} // namespace adayo
