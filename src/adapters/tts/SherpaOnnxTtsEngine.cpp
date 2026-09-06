#include "adapters/tts/SherpaOnnxTtsEngine.h"

#include "platform/UnicodePath.h"
#include "core/unicode/Utf8.h"
#include "platform/FileIo.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#ifdef ADAYO_HAS_SHERPA_ONNX
#include "sherpa-onnx/c-api/c-api.h"
#include "onnxruntime_c_api.h"
#endif

namespace adayo {
namespace {

std::filesystem::path RequiredPathFromUtf8(const std::string& value, const char* label) {
    const auto path = PathFromUtf8(value);
    RequireOrdinaryPath(path);
    if (path.empty() || !std::filesystem::exists(path)) {
        throw std::runtime_error("sherpa-onnx 模型缺失 " + std::string(label) + ": " + PathToUtf8(path));
    }
    return path;
}

#ifdef ADAYO_HAS_SHERPA_ONNX
using Metadata=std::map<std::string,std::string>;
[[noreturn]] void Reject(const std::string& message) {
    throw std::runtime_error("MODEL_ADMISSION: "+message);
}
int Integer(std::string_view value,const std::string& label) {
    int result{};
    const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    if(value.empty() || parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || result<0)
        Reject(label+" must be a nonnegative int32");
    return result;
}
std::string ReadUtf8Resource(const std::filesystem::path& path,std::uintmax_t limit) {
    RequireOrdinaryPath(path);
    if(!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path)>limit)
        Reject("invalid resource size: "+PathToUtf8(path));
    std::ifstream input(path,std::ios::binary);
    if(!input) Reject("cannot read "+PathToUtf8(path));
    std::string bytes{std::istreambuf_iterator<char>(input),{}};
    if(input.bad() || bytes.find('\0')!=std::string::npos) Reject("invalid resource bytes: "+PathToUtf8(path));
    unicode::DecodeStrict(bytes);
    return bytes;
}
const OrtApi& Ort() {
    const auto* api=OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if(!api) Reject("ONNX Runtime API version does not match development headers");
    if(std::strcmp(OrtGetApiBase()->GetVersionString(),SherpaOnnxGetOnnxruntimeVersionStr())!=0)
        Reject("ONNX Runtime differs from the pinned Sherpa runtime");
    return *api;
}
void OrtCheck(OrtStatus* status) {
    if(!status) return;
    const std::string error=Ort().GetErrorMessage(status);
    Ort().ReleaseStatus(status);
    Reject("ONNX Runtime: "+error);
}
Metadata ReadMetadata(const std::filesystem::path& model) {
    const auto& api=Ort();
    OrtEnv* env_raw{}; OrtCheck(api.CreateEnv(ORT_LOGGING_LEVEL_ERROR,"adayo-admission",&env_raw));
    std::unique_ptr<OrtEnv,decltype(api.ReleaseEnv)> env(env_raw,api.ReleaseEnv);
    OrtSessionOptions* options_raw{}; OrtCheck(api.CreateSessionOptions(&options_raw));
    std::unique_ptr<OrtSessionOptions,decltype(api.ReleaseSessionOptions)> options(options_raw,api.ReleaseSessionOptions);
    OrtCheck(api.SetIntraOpNumThreads(options.get(),1));
    OrtCheck(api.SetInterOpNumThreads(options.get(),1));
    OrtCheck(api.SetSessionGraphOptimizationLevel(options.get(),ORT_DISABLE_ALL));
    OrtSession* session_raw{}; OrtCheck(api.CreateSession(env.get(),model.c_str(),options.get(),&session_raw));
    std::unique_ptr<OrtSession,decltype(api.ReleaseSession)> session(session_raw,api.ReleaseSession);
    OrtModelMetadata* metadata_raw{}; OrtCheck(api.SessionGetModelMetadata(session.get(),&metadata_raw));
    std::unique_ptr<OrtModelMetadata,decltype(api.ReleaseModelMetadata)> metadata(metadata_raw,api.ReleaseModelMetadata);
    OrtAllocator* allocator{}; OrtCheck(api.GetAllocatorWithDefaultOptions(&allocator));
    Metadata values;
    for(const char* key:{"sample_rate","n_speakers","language","comment","voice","frontend","punctuation",
        "add_blank","speaker_id","version","num_emotions","jieba","blank_id","bos_id","eos_id","use_eos_bos","pad_id","has_g2pw"}) {
        char* value{}; OrtCheck(api.ModelMetadataLookupCustomMetadataMap(metadata.get(),allocator,key,&value));
        if(value) {
            auto release=[allocator](char* p) { allocator->Free(allocator,p); };
            std::unique_ptr<char,decltype(release)> owned(value,release);
            values.emplace(key,value);
        }
    }
    size_t inputs{},outputs{};
    OrtCheck(api.SessionGetInputCount(session.get(),&inputs));
    OrtCheck(api.SessionGetOutputCount(session.get(),&outputs));
    if(inputs==0 || outputs!=1) Reject("VITS graph input/output contract mismatch");
    return values;
}
int MetaInt(const Metadata& metadata,const std::string& key,int fallback=0) {
    const auto it=metadata.find(key);
    return it==metadata.end() ? fallback : Integer(it->second,"metadata "+key);
}
std::string MetaString(const Metadata& metadata,const std::string& key) {
    const auto it=metadata.find(key); return it==metadata.end() ? std::string{} : it->second;
}
void ValidateMetadata(const Metadata& metadata) {
    for(const char* key:{"sample_rate","n_speakers","language","comment"}) {
        if(!metadata.contains(key) || metadata.at(key).empty()) Reject("missing metadata "+std::string(key));
    }
    for(const auto& [key,value]:metadata) unicode::DecodeStrict(value);
    const auto rate=MetaInt(metadata,"sample_rate");
    if(rate<8000 || rate>384000 || MetaInt(metadata,"n_speakers")<1) Reject("invalid sample_rate or n_speakers");
    for(const char* key:{"speaker_id","version","num_emotions","blank_id","bos_id","eos_id","pad_id"}) MetaInt(metadata,key);
    for(const char* key:{"add_blank","jieba","use_eos_bos","has_g2pw"})
        if(MetaInt(metadata,key,key==std::string("use_eos_bos") ? 1 : 0)>1) Reject("invalid metadata flag "+std::string(key));
    if(MetaString(metadata,"comment").find("melo")!=std::string::npos && MetaInt(metadata,"version")<2)
        Reject("Melo frontend requires model version >= 2");
}
void ValidateFrontend(const TtsModelConfig& config,const Metadata& metadata) {
    const bool characters=MetaString(metadata,"frontend")=="characters";
    const bool pinyin=!characters && (MetaInt(metadata,"jieba") || MetaInt(metadata,"has_g2pw"));
    const auto comment=MetaString(metadata,"comment");
    const bool espeak=!characters && !pinyin && !config.data_dir.empty() &&
        (comment.find("piper")!=std::string::npos || comment.find("coqui")!=std::string::npos ||
         comment.find("icefall")!=std::string::npos || comment.find("Inflect")!=std::string::npos);
    if(!characters && !espeak && config.lexicon_path.empty()) Reject("lexicon required by selected frontend");
    std::map<std::string,int> tokens;
    std::istringstream lines(ReadUtf8Resource(PathFromUtf8(config.tokens_path),16*1024*1024));
    std::string line; size_t number{};
    while(std::getline(lines,line)) {
        ++number;
        if(!line.empty() && line.back()=='\r') line.pop_back();
        const auto end=line.find_last_not_of(" \t");
        if(end==std::string::npos) Reject("empty token line "+std::to_string(number));
        line.resize(end+1);
        const auto separator=line.find_last_of(" \t");
        if(separator==std::string::npos) Reject("token line requires symbol and ID at "+std::to_string(number));
        const auto id=Integer(std::string_view(line).substr(separator+1),"token line "+std::to_string(number));
        auto symbol=line.substr(0,separator);
        if(symbol.find_first_not_of(" \t")==std::string::npos) symbol=" ";
        else {
            const auto first=symbol.find_first_not_of(" \t"),last=symbol.find_last_not_of(" \t");
            symbol=symbol.substr(first,last-first+1);
            if(symbol.find_first_of(" \t\v\f\r")!=std::string::npos) Reject("extra token fields at "+std::to_string(number));
        }
        const auto decoded=unicode::DecodeStrict(symbol);
        const bool special=symbol=="<BLNK>" || (characters && (symbol=="<PAD>" || symbol=="<EOS>" || symbol=="<BOS>"));
        if((characters || espeak) && !special && decoded.size()!=1)
            Reject("frontend requires one Unicode scalar at token line "+std::to_string(number));
        if(!tokens.emplace(symbol,id).second) Reject("duplicate token at line "+std::to_string(number));
    }
    if(tokens.empty()) Reject("empty token table");
    if((espeak && comment.find("piper")!=std::string::npos) || (pinyin && MetaInt(metadata,"has_g2pw"))) {
        for(const char* symbol:{"_","^","$"}) if(!tokens.contains(symbol)) Reject("missing required token "+std::string(symbol));
    }
    if(characters || (espeak && comment.find("coqui")!=std::string::npos)) {
        std::set<int> ids;
        for(const auto& [symbol,id]:tokens) ids.insert(id);
        if(MetaInt(metadata,"use_eos_bos",1)) for(const char* key:{"bos_id","eos_id"})
            if(!ids.contains(MetaInt(metadata,key))) Reject("special metadata ID absent from tokens: "+std::string(key));
        if(MetaInt(metadata,"add_blank") && !ids.contains(MetaInt(metadata,"blank_id"))) Reject("blank_id absent from tokens");
    }
    if(espeak) {
        const auto root=PathFromUtf8(config.data_dir);
        for(const char* name:{"phondata","phonindex","phontab","intonations"}) {
            const auto path=root/name; RequireOrdinaryPath(path);
            if(!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path)==0)
                Reject("missing espeak data: "+std::string(name));
        }
        if(MetaString(metadata,"voice").empty()) Reject("espeak voice metadata is empty");
    }
    if(!config.lexicon_path.empty()) ReadUtf8Resource(PathFromUtf8(config.lexicon_path),256*1024*1024);
}
std::string AdmissionFingerprint(const TtsModelConfig& config) {
    std::string content;
    auto field=[&](const std::string& value) { content+=std::to_string(value.size())+":"+value; };
    for(const auto& value:{config.model_path,config.tokens_path,config.data_dir,config.lexicon_path,config.rule_fsts,config.text_normalization}) field(value);
    std::set<std::filesystem::path> files;
    for(const auto& value:{config.model_path,config.tokens_path,config.lexicon_path}) if(!value.empty()) files.insert(PathFromUtf8(value));
    std::istringstream rules(config.rule_fsts); std::string rule;
    while(std::getline(rules,rule,',')) {
        const auto first=rule.find_first_not_of(" \t"),last=rule.find_last_not_of(" \t");
        if(first!=std::string::npos) files.insert(PathFromUtf8(rule.substr(first,last-first+1)));
    }
    if(!config.data_dir.empty()) for(const auto& entry:std::filesystem::recursive_directory_iterator(PathFromUtf8(config.data_dir))) {
        RequireOrdinaryPath(entry.path()); if(entry.is_regular_file()) files.insert(entry.path());
    }
    for(const auto& file:files) { RequireOrdinaryPath(file); field(PathToUtf8(file)); field(FileSha256(file)); }
    return Sha256(content);
}
#endif

} // namespace

struct SherpaOnnxTtsEngine::Impl {
    TtsModelConfig config;
#ifdef ADAYO_HAS_SHERPA_ONNX
    const SherpaOnnxOfflineTts* tts{nullptr};
    std::map<std::string,Metadata> validated;
    int speakers{};
#ifdef _WIN32
    HANDLE resource_lease{INVALID_HANDLE_VALUE};
#endif
#endif
};

SherpaOnnxTtsEngine::SherpaOnnxTtsEngine() : impl_(std::make_unique<Impl>()) {}
SherpaOnnxTtsEngine::~SherpaOnnxTtsEngine() { Unload(); }

std::string SherpaOnnxTtsEngine::RuntimeIdentity() const {
#ifdef ADAYO_HAS_SHERPA_ONNX
    return std::string("sherpa-vits-v1|") + SherpaOnnxGetVersionStr() + "|" + SherpaOnnxGetOnnxruntimeVersionStr() +
        "|cpu|noise=.667,.8|length=1|silence=.2|max_sentences=2|float32-v1";
#else
    throw std::runtime_error("Sherpa runtime unavailable");
#endif
}

bool SherpaOnnxTtsEngine::IsLoaded() const noexcept {
#ifdef ADAYO_HAS_SHERPA_ONNX
    return impl_->tts != nullptr;
#else
    return false;
#endif
}

void SherpaOnnxTtsEngine::Load(const TtsModelConfig& config) {
    if (config.text_normalization != "none" && config.text_normalization != "nfd")
        throw std::invalid_argument("Unsupported TTS text normalization");
    Unload();
    impl_->config = config;
#ifdef ADAYO_HAS_SHERPA_ONNX
    const auto root=config.resource_root.empty() ? PathFromUtf8(config.model_path).parent_path() : PathFromUtf8(config.resource_root);
    RequireOrdinaryPath(root);
#ifdef _WIN32
    std::unique_ptr<void,decltype(&CloseHandle)> resource_lease(nullptr,CloseHandle);
    if(config.offline_transaction.empty() && std::filesystem::exists(root/"model.json")) {
        auto handle=CreateFileW((root/"model.json").c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE) Reject("voice config is owned by an offline update");
        resource_lease.reset(handle);
    }
#endif
    if(config.offline_transaction.empty() && std::filesystem::exists(root/".preparation-incomplete.json"))
        Reject("MODEL_UPDATE_INCOMPLETE: offline recovery required");
    RequiredPathFromUtf8(impl_->config.model_path, "model");
    RequiredPathFromUtf8(impl_->config.tokens_path, "tokens");
    if (!impl_->config.data_dir.empty()) {
        RequiredPathFromUtf8(impl_->config.data_dir, "data_dir");
    }
    if (!impl_->config.lexicon_path.empty()) {
        RequiredPathFromUtf8(impl_->config.lexicon_path, "lexicon");
    }

    const auto fingerprint=AdmissionFingerprint(config);
    auto admission=impl_->validated.find(fingerprint);
    if(admission==impl_->validated.end()) {
        auto metadata=ReadMetadata(PathFromUtf8(config.model_path));
        ValidateMetadata(metadata);
        ValidateFrontend(config,metadata);
        if(impl_->validated.size()>=64) impl_->validated.erase(impl_->validated.begin());
        admission=impl_->validated.emplace(fingerprint,std::move(metadata)).first;
    }
    impl_->speakers=MetaInt(admission->second,"n_speakers");
    if(config.speaker_id<0 || config.speaker_id>=impl_->speakers) Reject("configured speaker ID outside model range");

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
#ifdef _WIN32
    impl_->resource_lease=resource_lease.release();
#endif
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
#ifdef _WIN32
    if(impl_ && impl_->resource_lease!=INVALID_HANDLE_VALUE && impl_->resource_lease) {
        CloseHandle(impl_->resource_lease); impl_->resource_lease=INVALID_HANDLE_VALUE;
    }
#endif
#endif
}

AudioBuffer SherpaOnnxTtsEngine::Synthesize(const TtsRequest& request) {
    if (!std::isfinite(request.speed) || request.text.find('\0') != std::string::npos)
        throw std::invalid_argument("Invalid speed or embedded NUL in TTS request");
#ifdef ADAYO_HAS_SHERPA_ONNX
    if (!impl_->tts) throw std::runtime_error("sherpa-onnx TTS 未加载");
    if(request.speaker_id<0 || request.speaker_id>=impl_->speakers) Reject("requested speaker ID outside model range");
    unicode::DecodeStrict(request.text);
    SherpaOnnxGenerationConfig generation{};
    generation.sid = request.speaker_id;
    generation.speed = static_cast<float>(std::clamp(request.speed, 0.5, 2.0));

    using AudioPtr = std::unique_ptr<const SherpaOnnxGeneratedAudio, decltype(&SherpaOnnxDestroyOfflineTtsGeneratedAudio)>;
    const auto text = impl_->config.text_normalization == "nfd" ? unicode::NormalizeNfd(request.text) : request.text;
    const SherpaOnnxGeneratedAudio* raw_audio = SherpaOnnxOfflineTtsGenerateWithConfig(
        impl_->tts, text.c_str(), &generation, nullptr, nullptr);
    AudioPtr audio(raw_audio, SherpaOnnxDestroyOfflineTtsGeneratedAudio);
    if (!audio) throw std::runtime_error("sherpa-onnx TTS 生成失败");
    if (audio->sample_rate <= 0 || audio->n <= 0 || audio->samples == nullptr) {
        throw std::runtime_error("TTS_INVALID_AUDIO: sherpa-onnx 返回无效音频");
    }

    AudioBuffer result;
    result.sample_rate = audio->sample_rate;
    result.channels = 1;
    result.samples.assign(audio->samples, audio->samples + audio->n);
    return result;
#else
    (void)request;
    throw std::runtime_error("当前构建未启用 sherpa-onnx adapter");
#endif
}

} // namespace adayo
