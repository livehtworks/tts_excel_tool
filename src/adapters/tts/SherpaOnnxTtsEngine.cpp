#include "adapters/tts/SherpaOnnxTtsEngine.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <stdexcept>

#ifdef ADAYO_HAS_SHERPA_ONNX
#include "sherpa-onnx/c-api/c-api.h"
#endif

namespace adayo {

struct SherpaOnnxTtsEngine::Impl {
    TtsModelConfig config;
#ifdef ADAYO_HAS_SHERPA_ONNX
    const SherpaOnnxOfflineTts* tts{nullptr};
#endif
};

SherpaOnnxTtsEngine::SherpaOnnxTtsEngine() : impl_(std::make_unique<Impl>()) {}
SherpaOnnxTtsEngine::~SherpaOnnxTtsEngine() { Unload(); }

bool SherpaOnnxTtsEngine::IsLoaded() const noexcept {
#ifdef ADAYO_HAS_SHERPA_ONNX
    return impl_->tts != nullptr;
#else
    return false;
#endif
}

void SherpaOnnxTtsEngine::Load(const TtsModelConfig& config) {
    Unload();
    impl_->config = config;
#ifdef ADAYO_HAS_SHERPA_ONNX
    for (const auto& [path, label] : {
             std::pair{impl_->config.model_path, "model"},
             std::pair{impl_->config.tokens_path, "tokens"},
         }) {
        if (path.empty() || !std::filesystem::exists(path)) {
            throw std::runtime_error("sherpa-onnx 模型缺失 " + std::string(label) + ": " + path);
        }
    }
    if (!impl_->config.data_dir.empty() && !std::filesystem::exists(impl_->config.data_dir)) {
        throw std::runtime_error("sherpa-onnx 模型缺失 data_dir: " + impl_->config.data_dir);
    }

    SherpaOnnxOfflineTtsConfig c{};
    c.model.vits.model = impl_->config.model_path.c_str();
    c.model.vits.tokens = impl_->config.tokens_path.c_str();
    c.model.vits.data_dir = impl_->config.data_dir.c_str();
    c.model.vits.lexicon = impl_->config.lexicon_path.c_str();
    c.model.vits.noise_scale = 0.667f;
    c.model.vits.noise_scale_w = 0.8f;
    c.model.vits.length_scale = 1.0f;
    c.model.num_threads = std::max<std::int32_t>(1, impl_->config.num_threads);
    c.model.provider = "cpu";
    c.model.debug = 0;
    c.rule_fsts = impl_->config.rule_fsts.empty() ? nullptr : impl_->config.rule_fsts.c_str();
    c.max_num_sentences = 2;
    c.silence_scale = 0.2f;

    impl_->tts = SherpaOnnxCreateOfflineTts(&c);
    if (!impl_->tts) throw std::runtime_error("SherpaOnnxCreateOfflineTts 失败");
#else
    throw std::runtime_error("当前构建未启用 sherpa-onnx adapter");
#endif
}

void SherpaOnnxTtsEngine::Unload() noexcept {
#ifdef ADAYO_HAS_SHERPA_ONNX
    if (impl_ && impl_->tts) {
        SherpaOnnxDestroyOfflineTts(impl_->tts);
        impl_->tts = nullptr;
    }
#endif
}

AudioBuffer SherpaOnnxTtsEngine::Synthesize(const TtsRequest& request) {
#ifdef ADAYO_HAS_SHERPA_ONNX
    if (!impl_->tts) throw std::runtime_error("sherpa-onnx TTS 未加载");
    SherpaOnnxGenerationConfig generation{};
    generation.sid = request.speaker_id;
    generation.speed = static_cast<float>(std::clamp(request.speed, 0.5, 2.0));

    const SherpaOnnxGeneratedAudio* audio = SherpaOnnxOfflineTtsGenerateWithConfig(
        impl_->tts, request.text.c_str(), &generation, nullptr, nullptr);
    if (!audio) throw std::runtime_error("sherpa-onnx TTS 生成失败");

    AudioBuffer result;
    result.sample_rate = audio->sample_rate;
    result.channels = 1;
    result.samples.assign(audio->samples, audio->samples + audio->n);
    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
    return result;
#else
    (void)request;
    throw std::runtime_error("当前构建未启用 sherpa-onnx adapter");
#endif
}

} // namespace adayo
