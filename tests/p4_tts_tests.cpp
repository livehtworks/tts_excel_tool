#include "adapters/tts/SherpaOnnxTtsEngine.h"
#include "services/ModelRegistry.h"
#include "services/TtsService.h"

#include "TestCheck.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

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

void PrintLatencyStats(const char* label, std::vector<long long> latencies_ms) {
    REQUIRE(!latencies_ms.empty());
    std::sort(latencies_ms.begin(), latencies_ms.end());
    const auto total = std::accumulate(latencies_ms.begin(), latencies_ms.end(), 0LL);
    const double average = static_cast<double>(total) / static_cast<double>(latencies_ms.size());
    const std::size_t p95_index = (latencies_ms.size() * 95 + 99) / 100 - 1;
    std::cout << label
              << " count=" << latencies_ms.size()
              << " avg_ms=" << average
              << " p95_ms=" << latencies_ms[p95_index]
              << "\n";
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
    std::vector<long long> en_latencies;
    en_latencies.reserve(500);
    for (int i = 0; i < 500; ++i) {
        const auto item_start = std::chrono::steady_clock::now();
        RequireUsableAudio(service.Synthesize({"short stability sentence", en.config.language_code, 0, 1.0}));
        en_latencies.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - item_start).count());
        if ((i + 1) % 50 == 0) {
            std::cout << "generated=" << (i + 1) << "\n" << std::flush;
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    std::cout << "500 English sherpa generations elapsed_ms=" << elapsed << "\n";
    PrintLatencyStats("500 English sherpa latency", std::move(en_latencies));

    std::cout << "P4 stress: 500 Chinese\n" << std::flush;
    service.LoadModel(zh.config);
    const auto zh_start = std::chrono::steady_clock::now();
    std::vector<long long> zh_latencies;
    zh_latencies.reserve(500);
    for (int i = 0; i < 500; ++i) {
        const auto item_start = std::chrono::steady_clock::now();
        RequireUsableAudio(service.Synthesize({"短句稳定性测试", zh.config.language_code, 0, 1.0}));
        zh_latencies.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - item_start).count());
        if ((i + 1) % 50 == 0) {
            std::cout << "generated_zh=" << (i + 1) << "\n" << std::flush;
        }
    }
    const auto zh_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - zh_start).count();
    std::cout << "500 Chinese sherpa generations elapsed_ms=" << zh_elapsed << "\n";
    PrintLatencyStats("500 Chinese sherpa latency", std::move(zh_latencies));
}
} // namespace

int main() {
    return test::RunTestMain("adayo_p4_tts_tests", [] {
        TestSherpaSmokeAndSwitching();
        TestSherpaRepeatedGeneration();
    });
}
