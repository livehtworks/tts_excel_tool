#pragma once

#include "core/tts/ITtsEngine.h"

namespace adayo {

// Boundary is intentionally present now, but the ONNX graph orchestration is NOT fabricated here.
// Codex P8 must port the official MOSS-TTS-Nano standalone ONNX inference sequence to C++/ONNX Runtime
// and validate sample-by-sample parity before this engine is enabled in UI.
class MossNanoTtsEngine final : public ITtsEngine {
public:
    std::string Id() const override { return "moss-nano-onnx"; }
    bool IsLoaded() const noexcept override { return false; }
    void Load(const TtsModelConfig&) override;
    void Unload() noexcept override {}
    AudioBuffer Synthesize(const TtsRequest&) override;
};

} // namespace adayo
