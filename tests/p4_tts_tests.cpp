#include "adapters/tts/SherpaOnnxTtsEngine.h"
#include "services/ModelRegistry.h"
#include "services/TtsService.h"
#include "platform/FileIo.h"
#include "platform/UnicodePath.h"
#include <fstream>
#include <nlohmann/json.hpp>

#include "TestCheck.h"
#ifdef ADAYO_HAS_MINIAUDIO
#include "adapters/audio/MiniaudioPlayer.h"
#include <miniaudio.h>
#include <mutex>
#include <thread>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
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
    service.EnsureModelLoaded(en.id, en.config);
    const auto slow = service.Synthesize({"Hello from Adayo corpus tool.", en.config.language_code, 0, 0.5});
    const auto normal = service.Synthesize({"Hello from Adayo corpus tool.", en.config.language_code, 0, 1.0});
    const auto fast = service.Synthesize({"Hello from Adayo corpus tool.", en.config.language_code, 0, 2.0});
    RequireUsableAudio(slow);
    RequireUsableAudio(normal);
    RequireUsableAudio(fast);
    REQUIRE(slow.samples.size() > normal.samples.size());
    REQUIRE(normal.samples.size() > fast.samples.size());

    std::cout << "P4 smoke: Chinese\n" << std::flush;
    service.EnsureModelLoaded(zh.id, zh.config);
    RequireUsableAudio(service.Synthesize({"你好，欢迎使用语料测试工具。", zh.config.language_code, 0, 1.0}));

    std::cout << "P4 smoke: switching\n" << std::flush;
    for (int i = 0; i < 20; ++i) {
        service.EnsureModelLoaded(en.id, en.config);
        RequireUsableAudio(service.Synthesize({"Switch to English.", en.config.language_code, 0, 1.0}));
        service.EnsureModelLoaded(zh.id, zh.config);
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
    service.EnsureModelLoaded(en.id, en.config);

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
    service.EnsureModelLoaded(zh.id, zh.config);
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

void CopyDirectory(const std::filesystem::path& source, const std::filesystem::path& target) {
    std::filesystem::create_directories(target);
    std::filesystem::copy(source, target,
        std::filesystem::copy_options::recursive |
        std::filesystem::copy_options::overwrite_existing);
}

void TestSherpaUnicodePathGate() {
    const std::filesystem::path source_root = std::filesystem::path(ADAYO_MODELS_DIR) / "sherpa";
    const auto gate_root = test::IsolatedRoot() /
        PathFromUtf8("公司中文工具/Adayo语料测试/模型/中文语音模型");
    const auto unicode_sherpa_root = gate_root / "model" / "sherpa";
    std::filesystem::remove_all(gate_root);
    CopyDirectory(source_root / "vits-piper-en_US-amy-low", unicode_sherpa_root / PathFromUtf8("英文Voice目录"));
    CopyDirectory(source_root / "vits-piper-zh_CN-xiao_ya-medium-int8", unicode_sherpa_root / PathFromUtf8("中文Voice目录"));

    ModelRegistry registry(unicode_sherpa_root);
    const auto entries = registry.ScanSherpaModels();
    const auto& en = FindId(entries, "vits-piper-en_US-amy-low");
    const auto& zh = FindId(entries, "vits-piper-zh_CN-xiao_ya-medium-int8");

    TtsService service;
    service.SetEngine(std::make_unique<SherpaOnnxTtsEngine>());

    service.EnsureModelLoaded(en.id, en.config);
    for (int i = 0; i < 20; ++i) {
        RequireUsableAudio(service.Synthesize({"Unicode path English gate.", en.config.language_code, 0, 1.0}));
    }
    std::cout << "SHERPA_UNICODE_PATH_EN_PASS\n";

    service.EnsureModelLoaded(zh.id, zh.config);
    for (int i = 0; i < 20; ++i) {
        RequireUsableAudio(service.Synthesize({"中文路径语音门禁。", zh.config.language_code, 0, 1.0}));
    }
    std::cout << "SHERPA_UNICODE_PATH_ZH_PASS\n";

    service.EnsureModelLoaded(en.id, en.config);
    RequireUsableAudio(service.Synthesize({"Back to English.", en.config.language_code, 0, 1.0}));
    service.EnsureModelLoaded(zh.id, zh.config);
    RequireUsableAudio(service.Synthesize({"再切回中文。", zh.config.language_code, 0, 1.0}));
    service.EnsureModelLoaded(en.id, en.config);
    RequireUsableAudio(service.Synthesize({"English again.", en.config.language_code, 0, 1.0}));
    std::cout << "SHERPA_UNICODE_PATH_SWITCH_PASS\n";
}
} // namespace

#ifdef ADAYO_HAS_MINIAUDIO
struct LoopbackCapture {
    ma_context context{};
    ma_device device{};
    bool context_ready=false,device_ready=false;
    std::mutex mutex;
    std::vector<float> samples;
    ~LoopbackCapture() { if(device_ready) ma_device_uninit(&device); if(context_ready) ma_context_uninit(&context); }
    void Start(std::int32_t rate) {
        const ma_backend backend=ma_backend_wasapi;
        if(ma_context_init(&backend,1,nullptr,&context)!=MA_SUCCESS) throw std::runtime_error("BLOCKED: WASAPI context unavailable");
        context_ready=true;
        auto config=ma_device_config_init(ma_device_type_loopback);
        config.capture.format=ma_format_f32; config.capture.channels=1; config.sampleRate=rate;
        config.pUserData=this;
        config.dataCallback=[](ma_device* device,void*,const void* input,ma_uint32 frames) {
            if(!input) return;
            auto& self=*static_cast<LoopbackCapture*>(device->pUserData);
            std::lock_guard lock(self.mutex);
            if(self.samples.size()+frames>384000*120ull) return;
            const auto* values=static_cast<const float*>(input);
            self.samples.insert(self.samples.end(),values,values+frames);
        };
        samples.reserve(static_cast<std::size_t>(rate)*30);
        if(ma_device_init(&context,&config,&device)!=MA_SUCCESS) throw std::runtime_error("BLOCKED: WASAPI loopback initialization failed");
        device_ready=true;
        if(ma_device_start(&device)!=MA_SUCCESS) throw std::runtime_error("BLOCKED: WASAPI loopback start failed");
    }
    std::vector<float> Finish() {
        if(ma_device_stop(&device)!=MA_SUCCESS) throw std::runtime_error("Loopback stop failed");
        std::lock_guard lock(mutex); return samples;
    }
};
double Correlation(const std::vector<float>& expected,const std::vector<float>& captured,std::size_t begin,std::size_t end,
                   std::size_t offset) {
    double dot=0,xx=0,yy=0;
    for(auto i=begin;i<end;i+=8) {
        const auto j=i+offset; if(j>=captured.size()) return 0;
        dot+=expected[i]*captured[j]; xx+=expected[i]*expected[i]; yy+=captured[j]*captured[j];
    }
    return xx>0 && yy>0?dot/std::sqrt(xx*yy):0;
}
void VerifyLoopback(const AudioBuffer& audio,const std::vector<float>& captured,const std::filesystem::path& path) {
    REQUIRE(audio.channels==1); REQUIRE(!captured.empty());
    ma_encoder encoder{};
    const auto config=ma_encoder_config_init(ma_encoding_format_wav,ma_format_f32,1,audio.sample_rate);
    REQUIRE(ma_encoder_init_file_w(path.c_str(),&config,&encoder)==MA_SUCCESS);
    ma_uint64 written=0;
    const auto write=ma_encoder_write_pcm_frames(&encoder,captured.data(),captured.size(),&written);
    ma_encoder_uninit(&encoder);
    REQUIRE(write==MA_SUCCESS); REQUIRE(written==captured.size());
    const auto& expected=audio.samples;
    auto first=std::find_if(expected.begin(),expected.end(),[](float v){return std::abs(v)>0.01f;});
    auto last=std::find_if(expected.rbegin(),expected.rend(),[](float v){return std::abs(v)>0.01f;});
    REQUIRE(first!=expected.end()); REQUIRE(last!=expected.rend());
    const auto begin=static_cast<std::size_t>(first-expected.begin());
    const auto end=expected.size()-static_cast<std::size_t>(last-expected.rbegin());
    const auto window=std::min(static_cast<std::size_t>(audio.sample_rate/4),(end-begin)/2);
    double best=0;std::size_t offset=0;
    for(std::size_t candidate=0;candidate<static_cast<std::size_t>(audio.sample_rate);++candidate) {
        const auto value=Correlation(expected,captured,begin,begin+window,candidate);
        if(value>best) { best=value;offset=candidate; }
    }
    const auto tail=Correlation(expected,captured,end-window,end,offset);
    std::cout<<"LOOPBACK head_correlation="<<best<<",tail_correlation="<<tail<<",offset_frames="<<offset<<"\n";
    REQUIRE(best>0.7); REQUIRE(tail>0.7); REQUIRE(captured.size()>=end+offset);
}
#endif

static int RunMain(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--registry-probe") {
        return test::RunTestMain("adayo_downloaded_voice_registry", [&] {
            const auto scan = ModelRegistry(PathFromUtf8(argv[2])).ScanSherpaModelsWithDiagnostics();
            std::ifstream input(PathFromUtf8(argv[3]));
            const auto inventory = nlohmann::json::parse(input);
            std::size_t expected = 0, models = 0;
            for (const auto& row : inventory.at("piper")) {
                const auto id = row.at("id").get<std::string>();
                if (row.at("status") != "PASS") {
                    REQUIRE(std::none_of(scan.entries.begin(), scan.entries.end(), [&](const auto& e) { return e.id == id; }));
                    continue;
                }
                const auto& base = FindId(scan.entries, id);
                const auto count = row.at("num_speakers").get<int>();
                for (int sid = 0; sid < count; ++sid) {
                    const auto& entry = FindId(scan.entries, sid == base.config.speaker_id ? id : id + "::speaker-" + std::to_string(sid));
                    REQUIRE(entry.config.speaker_id == sid);
                    REQUIRE(entry.config.model_path == base.config.model_path);
                    REQUIRE(entry.config.language_code == row.at("language_code").get<std::string>());
                    ++expected;
                }
                ++models;
            }
            std::cout << "REGISTRY_OK models=" << models << " speakers=" << expected
                      << " total_entries=" << scan.entries.size() << " diagnostics=" << scan.invalid.size() << '\n';
            for (const auto& invalid : scan.invalid) std::cout << "DIAGNOSTIC " << invalid.id << ": " << invalid.error << '\n';
        });
    }
    if (argc == 4 && (std::string(argv[1]) == "--voice-probe" || std::string(argv[1]) == "--voice-probe-default")) {
        return test::RunTestMain("adayo_voice_probe", [&] {
            const auto model = ModelRegistry::LoadModelJson(PathFromUtf8(argv[2]));
            std::ifstream input(PathFromUtf8(argv[3]));
            const auto fixture = nlohmann::json::parse(input);
            const auto text = fixture.at(model.config.language_code).get<std::string>();
            TtsService service;
            service.SetEngine(std::make_unique<SherpaOnnxTtsEngine>());
            service.EnsureModelLoaded(model.id, model.config);
            auto speakers = model.speakers;
            if (std::string(argv[1]) == "--voice-probe-default") speakers.clear();
            if (speakers.empty()) speakers.emplace_back(model.config.speaker_id, "default");
            for (const auto& [id, name] : speakers) {
                const auto audio = service.Synthesize({text, model.config.language_code, id, 1.0});
                RequireUsableAudio(audio);
                double energy = 0;
                for (float value : audio.samples) { REQUIRE(std::isfinite(value)); energy += value * value; }
                REQUIRE(audio.samples.size() >= static_cast<std::size_t>(audio.sample_rate / 10));
                REQUIRE(energy / audio.samples.size() > 1e-8);
                std::cout << "VOICE_OK " << model.id << " speaker=" << id << " rate=" << audio.sample_rate
                          << " frames=" << audio.samples.size() << " rms=" << std::sqrt(energy / audio.samples.size()) << '\n' << std::flush;
            }
        });
    }
    if(argc==6 && (std::string(argv[1])=="--cache-probe" || std::string(argv[1])=="--audio-probe")) {
        return test::RunTestMain("adayo_p4_real_cache_probe",[&] {
            const auto probe_started=std::chrono::steady_clock::now();
            const auto root=PathFromUtf8(argv[2]);
            const auto mode=std::string(argv[3]), language=std::string(argv[4]);
            const bool loopback=std::string(argv[1])=="--audio-probe";
#ifdef ADAYO_HAS_MINIAUDIO
            MiniaudioPlayer player;
#else
            if(loopback) throw std::runtime_error("BLOCKED: miniaudio unavailable");
#endif
            std::ifstream input(PathFromUtf8(argv[5])); const auto fixture=nlohmann::json::parse(input);
            ModelRegistry registry(std::filesystem::path(ADAYO_MODELS_DIR)/"sherpa");
            const auto entries=registry.ScanSherpaModels();
            const auto& model=language=="zh-CN" ? FindId(entries,"vits-piper-zh_CN-xiao_ya-medium-int8") : FindLanguage(entries,language);
            TtsService service; service.SetEngine(std::make_unique<SherpaOnnxTtsEngine>());
            AudioCacheOptions options; options.enabled=mode!="baseline";
            service.InitializeCache(root/language/"tts-v1",options);
            REQUIRE(service.Cache()->Stats().disk_enabled);
            if(mode=="baseline") service.EnsureModelLoaded(model.id,model.config);
            std::cout << "request_id,language,mode,key_build_ms,model_validation_ms,lookup_ms,model_load_ms,synth_ms,cache_read_ms,cache_write_ms,audio_prepare_ms,source,load_call_delta,synth_call_delta,pcm_sha256\n";
            std::size_t index=0;
            for(const auto& sample:fixture.at("samples")) {
                if(sample.at("language_code").get<std::string>()!=language) continue;
                TtsRequest request{sample.at("text").get<std::string>(),language,model.config.speaker_id,1.0};
                if(mode=="memory") service.Prepare(model.id,model.config,request);
                const auto item_begin=std::chrono::steady_clock::now();
                auto result=service.Prepare(model.id,model.config,request);
                RequireUsableAudio(*result.audio);
                if(index==0) std::cout<<"FIRST_READY total_from_probe_entry_ms="
                    <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-probe_started).count()<<"\n";
                if(mode=="memory" || mode=="disk") {
                    REQUIRE(result.timings.source==mode); REQUIRE(result.timings.load_call_delta==0); REQUIRE(result.timings.synth_call_delta==0);
                } else { REQUIRE(result.timings.source=="synth"); REQUIRE(result.timings.load_call_delta==0); }
                const auto& t=result.timings;
                const auto hash=Sha256(std::string_view(reinterpret_cast<const char*>(result.audio->samples.data()),result.audio->samples.size()*sizeof(float)));
#ifdef ADAYO_HAS_MINIAUDIO
                if(loopback) {
                    LoopbackCapture capture; capture.Start(result.audio->sample_rate);
                    std::this_thread::sleep_for(std::chrono::milliseconds(150));
                    auto context=std::make_shared<AudioPlaybackContext>();
                    context->request_id=index;
                    context->item_started=item_begin;
                    player.Play(*result.audio,context);
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                    const auto captured=capture.Finish();
                    const auto audio_root=root/language/"loopback"/mode;
                    std::filesystem::create_directories(audio_root);
                    VerifyLoopback(*result.audio,captured,audio_root/(std::to_string(index)+".wav"));
                    REQUIRE(context->first_nonzero_ms); REQUIRE(context->playback_done_ms);
                    std::cout<<"DEVICE request="<<index<<",device_init_ms="<<context->device_init_ms<<",first_nonzero_ms="<<*context->first_nonzero_ms
                        <<",playback_done_ms="<<*context->playback_done_ms<<"\n";
                }
#endif
                std::cout << index++ << ',' << language << ',' << mode << ',' << t.key_build_ms << ',' << t.model_validation_ms << ',' << t.lookup_ms << ',' << t.model_load_ms << ','
                    << t.synth_ms << ',' << t.cache_read_ms << ',' << t.cache_write_ms << ',' << t.audio_prepare_ms << ',' << t.source << ',' << t.load_call_delta << ',' << t.synth_call_delta << ',' << hash << '\n';
            }
            REQUIRE(index>=20);
        });
    }
    return test::RunTestMain("adayo_p4_tts_tests", [] {
        // espeak has process-global initialization: Unicode paths must be first,
        // otherwise an earlier ASCII path can conceal an encoding failure.
        TestSherpaUnicodePathGate();
        TestSherpaSmokeAndSwitching();
        TestSherpaRepeatedGeneration();
    });
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> arguments;
    arguments.reserve(argc);
    for (int i = 0; i < argc; ++i) arguments.push_back(PathToUtf8(std::filesystem::path(argv[i])));
    std::vector<char*> pointers;
    for (auto& argument : arguments) pointers.push_back(argument.data());
    pointers.push_back(nullptr);
    return RunMain(argc, pointers.data());
}
#else
int main(int argc, char** argv) { return RunMain(argc, argv); }
#endif
