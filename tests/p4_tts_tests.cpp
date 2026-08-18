#include "adapters/tts/SherpaOnnxTtsEngine.h"
#include "services/ModelRegistry.h"
#include "services/TtsService.h"

#include "TestCheck.h"

#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace adayo;

namespace {
const TtsModelEntry& FindLanguage(const std::vector<TtsModelEntry>& entries, const std::string& language) {
    for (const auto& entry : entries) {
        if (entry.config.language_code == language) return entry;
    }
    throw std::runtime_error("Missing TTS model language: " + language);
}

const TtsModelEntry& FindId(const std::vector<TtsModelEntry>& entries, const std::string& id) {
    for (const auto& entry : entries) {
        if (entry.id == id) return entry;
    }
    throw std::runtime_error("Missing TTS model id: " + id);
}

void RequireUsableAudio(const AudioBuffer& audio) {
    REQUIRE(audio.sample_rate >= 8000);
    REQUIRE(audio.channels == 1);
    REQUIRE(!audio.samples.empty());
}

void TestSherpaSmokeAndSwitching() {
    std::cout << "P4 smoke: scan models\n" << std::flush;
    ModelRegistry registry(std::filesystem::path(ADAYO_MODELS_DIR) / "sherpa");
    const auto entries = registry.ScanSherpaModels();
    REQUIRE(entries.size() >= 2);
    const auto& en = FindLanguage(entries, "en-US");
    const auto& zh = FindId(entries, "vits-piper-zh_CN-xiao_ya-medium-int8");

    TtsService service;
    service.SetEngine(std::make_unique<SherpaOnnxTtsEngine>());

    std::cout << "P4 smoke: English speeds\n" << std::flush;
    service.LoadModel(en.config);
    const auto slow = service.Synthesize({"Hello from Adayo corpus tool.", en.config.language_code, 0, 0.5});
    const auto normal = service.Synthesize({"Hello from Adayo corpus tool.", en.config.language_code, 0, 1.0});
    const auto fast = service.Synthesize({"Hello from Adayo corpus tool.", en.config.language_code, 0, 2.0});
    RequireUsableAudio(slow);
    RequireUsableAudio(normal);
    RequireUsableAudio(fast);
    REQUIRE(slow.samples.size() > normal.samples.size());
    REQUIRE(normal.samples.size() > fast.samples.size());

    std::cout << "P4 smoke: Chinese\n" << std::flush;
    service.LoadModel(zh.config);
    RequireUsableAudio(service.Synthesize({"你好，欢迎使用语料测试工具。", zh.config.language_code, 0, 1.0}));

    std::cout << "P4 smoke: switching\n" << std::flush;
    for (int i = 0; i < 20; ++i) {
        service.LoadModel(en.config);
        RequireUsableAudio(service.Synthesize({"Switch to English.", en.config.language_code, 0, 1.0}));
        service.LoadModel(zh.config);
        RequireUsableAudio(service.Synthesize({"切换到中文。", zh.config.language_code, 0, 1.0}));
    }
}

void TestSherpaRepeatedGeneration() {
    std::cout << "P4 stress: 500 English\n" << std::flush;
    ModelRegistry registry(std::filesystem::path(ADAYO_MODELS_DIR) / "sherpa");
    const auto entries = registry.ScanSherpaModels();
    const auto& en = FindLanguage(entries, "en-US");
    const auto& zh = FindId(entries, "vits-piper-zh_CN-xiao_ya-medium-int8");
    TtsService service;
    service.SetEngine(std::make_unique<SherpaOnnxTtsEngine>());
    service.LoadModel(en.config);

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 500; ++i) {
        RequireUsableAudio(service.Synthesize({"short stability sentence", en.config.language_code, 0, 1.0}));
        if ((i + 1) % 50 == 0) {
            std::cout << "generated=" << (i + 1) << "\n" << std::flush;
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    std::cout << "500 English sherpa generations elapsed_ms=" << elapsed << "\n";

    std::cout << "P4 stress: 500 Chinese\n" << std::flush;
    service.LoadModel(zh.config);
    const auto zh_start = std::chrono::steady_clock::now();
    for (int i = 0; i < 500; ++i) {
        RequireUsableAudio(service.Synthesize({"短句稳定性测试", zh.config.language_code, 0, 1.0}));
        if ((i + 1) % 50 == 0) {
            std::cout << "generated_zh=" << (i + 1) << "\n" << std::flush;
        }
    }
    const auto zh_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - zh_start).count();
    std::cout << "500 Chinese sherpa generations elapsed_ms=" << zh_elapsed << "\n";
}
} // namespace

int main() {
    return test::RunTestMain("adayo_p4_tts_tests", [] {
        TestSherpaSmokeAndSwitching();
        TestSherpaRepeatedGeneration();
    });
}
