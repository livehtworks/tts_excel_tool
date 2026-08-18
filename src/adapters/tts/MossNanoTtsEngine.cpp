#include "adapters/tts/MossNanoTtsEngine.h"

#include <stdexcept>

namespace adayo {
void MossNanoTtsEngine::Load(const TtsModelConfig&) {
    throw std::runtime_error("MOSS Nano ONNX C++ adapter 尚未完成官方推理序列等价验证，禁止启用");
}
AudioBuffer MossNanoTtsEngine::Synthesize(const TtsRequest&) {
    throw std::runtime_error("MOSS Nano ONNX C++ adapter 尚未完成官方推理序列等价验证，禁止启用");
}
} // namespace adayo
