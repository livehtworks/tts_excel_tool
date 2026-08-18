#pragma once

#include "core/tts/ITtsEngine.h"

#include <memory>
#include <string>

namespace adayo {

class SherpaOnnxTtsEngine final : public ITtsEngine {
public:
    SherpaOnnxTtsEngine();
    ~SherpaOnnxTtsEngine() override;

    std::string Id() const override { return "sherpa-vits"; }
    bool IsLoaded() const noexcept override;
    void Load(const TtsModelConfig& config) override;
    void Unload() noexcept override;
    AudioBuffer Synthesize(const TtsRequest& request) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace adayo
