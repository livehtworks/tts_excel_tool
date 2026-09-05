#include "services/TtsService.h"
#include "platform/FileIo.h"
#include "platform/UnicodePath.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace adayo {
namespace {
using Clock = std::chrono::steady_clock;
double Ms(Clock::time_point start) { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); }
void Field(std::string& out, std::string_view value) { out += std::to_string(value.size()) + ":"; out.append(value); }
void CheckRequest(const TtsRequest& request) {
    if (!std::isfinite(request.speed) || request.text.find('\0')!=std::string::npos || request.speaker_id<0)
        throw std::invalid_argument("Invalid TTS request: speed, NUL or speaker");
}
}
void TtsService::SetEngine(std::unique_ptr<ITtsEngine> engine) {
    if (!engine) throw std::invalid_argument("TTS engine is required");
    std::scoped_lock lock(mutex_);
    if (engine_) engine_->Unload();
    engine_=std::move(engine); active_model_id_.clear(); active_identity_.clear(); fingerprints_.clear();
}
void TtsService::InitializeCache(const std::filesystem::path& root, AudioCacheOptions options) {
    std::lock_guard lock(mutex_);
    if (cache_) throw std::logic_error("Cache already initialized");
    cache_=std::make_unique<AudioCache>(root,options);
}
std::string TtsService::ModelIdentity(const TtsModelConfig& config, bool require_assets) {
    if (!engine_ || engine_->Id()!=config.engine_id) throw std::runtime_error("ENGINE_MISMATCH");
    std::string configuration;
    for (const auto& value : {config.engine_id,engine_->RuntimeIdentity(),config.model_path,config.tokens_path,
        config.data_dir,config.lexicon_path,config.rule_fsts,config.language_code,std::to_string(config.speaker_id),std::to_string(config.num_threads)})
        Field(configuration,value);
    if (require_assets && (config.model_path.empty() || config.tokens_path.empty() || config.language_code.empty() || config.speaker_id<0 || config.num_threads<1))
        throw std::runtime_error("MODEL_INVALID: incomplete model context");
    std::vector<std::filesystem::path> paths;
    auto addFile=[&](const std::string& value) {
        if (value.empty()) return;
        const auto path=PathFromUtf8(value);
        RequireOrdinaryPath(path);
        if (!std::filesystem::is_regular_file(path)) {
            if (require_assets) throw std::runtime_error("MODEL_MISSING: "+value);
            return;
        }
        paths.push_back(path);
    };
    addFile(config.model_path); addFile(config.tokens_path); addFile(config.lexicon_path);
    std::istringstream rules(config.rule_fsts); std::string rule;
    while (std::getline(rules,rule,',')) {
        auto first=rule.find_first_not_of(" \t"), last=rule.find_last_not_of(" \t");
        if (first!=std::string::npos) addFile(rule.substr(first,last-first+1));
    }
    if (!config.data_dir.empty()) {
        auto root=PathFromUtf8(config.data_dir); RequireOrdinaryPath(root);
        if (!std::filesystem::is_directory(root)) throw std::runtime_error("MODEL_MISSING: data_dir");
        for (const auto& item:std::filesystem::recursive_directory_iterator(root)) {
#ifdef _WIN32
            const auto attributes=GetFileAttributesW(item.path().c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT)) throw std::runtime_error("Model resource reparse entry rejected");
#else
            if(item.is_symlink()) throw std::runtime_error("Model resource symlink rejected");
#endif
            if (item.is_regular_file()) paths.push_back(item.path());
        }
    }
    std::sort(paths.begin(),paths.end());
    std::string snapshot=configuration;
    for (const auto& path:paths) {
        Field(snapshot,PathToUtf8(path));
#ifdef _WIN32
        auto handle=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (handle==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot validate model file identity");
        BY_HANDLE_FILE_INFORMATION info{}; auto ok=GetFileInformationByHandle(handle,&info); CloseHandle(handle);
        if (!ok) throw std::runtime_error("Cannot read model file identity");
        if(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) throw std::runtime_error("Model reparse replacement rejected");
        Field(snapshot,std::to_string(info.nFileSizeHigh)); Field(snapshot,std::to_string(info.nFileSizeLow));
        Field(snapshot,std::to_string(info.ftLastWriteTime.dwHighDateTime)); Field(snapshot,std::to_string(info.ftLastWriteTime.dwLowDateTime));
        Field(snapshot,std::to_string(info.dwVolumeSerialNumber));
        Field(snapshot,std::to_string(info.nFileIndexHigh)); Field(snapshot,std::to_string(info.nFileIndexLow));
#else
        Field(snapshot,std::to_string(std::filesystem::file_size(path)));
        Field(snapshot,std::to_string(std::filesystem::last_write_time(path).time_since_epoch().count()));
#endif
    }
    auto& cached=fingerprints_[configuration];
    if (cached.snapshot==snapshot && !cached.digest.empty()) return cached.digest;
    std::string content=configuration;
    for (const auto& path:paths) Field(content,FileSha256(path));
    cached={snapshot,Sha256(content)};
    return cached.digest;
}
void TtsService::EnsureLocked(std::string_view model_id, const TtsModelConfig& config, const std::string& identity) {
    if (!engine_ || config.engine_id!=engine_->Id()) throw std::runtime_error("ENGINE_MISMATCH");
    if (engine_->IsLoaded() && active_model_id_==model_id && active_identity_==identity) return;
    active_model_id_.clear(); active_identity_.clear();
    if (engine_->IsLoaded()) engine_->Unload();
    engine_->Load(config);
    active_model_id_=model_id; active_identity_=identity;
}
void TtsService::EnsureModelLoaded(std::string_view model_id, const TtsModelConfig& config) {
    std::lock_guard lock(mutex_);
    EnsureLocked(model_id,config,ModelIdentity(config,cache_!=nullptr));
}
AudioBuffer TtsService::Synthesize(const TtsRequest& request) {
    CheckRequest(request);
    std::lock_guard lock(mutex_);
    if (!engine_ || !engine_->IsLoaded()) throw std::runtime_error("TTS model not loaded");
    auto audio=engine_->Synthesize(request); AudioCache::Validate(audio); return audio;
}
PreparedAudio TtsService::Prepare(std::string_view model_id, const TtsModelConfig& config, const TtsRequest& request, std::stop_token token) {
    CheckRequest(request);
    PreparedAudio result;
    const auto start=Clock::now();
    // Taking the epoch before waiting prevents pre-clear misses from repopulating the cache.
    const auto epoch=cache_ ? cache_->Stats().epoch : 0;
    const bool clearAtStart=cache_ && cache_->Stats().clearing;
    std::unique_lock<std::timed_mutex> lock(mutex_,std::defer_lock);
    while (!lock.try_lock_for(std::chrono::milliseconds(10))) if (token.stop_requested()) throw std::runtime_error("TTS_CANCELED");
    if (token.stop_requested()) throw std::runtime_error("TTS_CANCELED");
    if (cache_ && (request.language_code!=config.language_code || request.speaker_id<0))
        throw std::runtime_error("MODEL_INVALID: language or speaker mismatch");
    auto checkpoint=Clock::now();
    const auto fingerprint=ModelIdentity(config,cache_!=nullptr);
    result.timings.model_validation_ms=Ms(checkpoint);
    checkpoint=Clock::now();
    auto effective=request; effective.speed=static_cast<float>(std::clamp(request.speed,0.5,2.0));
    std::string keyData;
    for (const auto& field:{std::string("tts-cache-v1"),std::string(model_id),fingerprint,request.text,request.language_code,
        std::to_string(request.speaker_id),std::to_string(std::bit_cast<std::uint32_t>(static_cast<float>(effective.speed)))}) Field(keyData,field);
    result.timings.key=Sha256(keyData); result.timings.key_build_ms=Ms(checkpoint);
    checkpoint=Clock::now();
    if (cache_ && !clearAtStart) {
        auto hit=cache_->Get(result.timings.key,fingerprint,epoch);
        if (hit.audio) { result.audio=std::move(hit.audio); result.timings.source=hit.source; }
    }
    result.timings.lookup_ms=Ms(checkpoint);
    if (result.audio) {
        if (result.timings.source=="disk") result.timings.cache_read_ms=result.timings.lookup_ms;
    } else {
        if (token.stop_requested()) throw std::runtime_error("TTS_CANCELED");
        checkpoint=Clock::now();
        const bool needsLoad=!engine_->IsLoaded() || active_model_id_!=model_id || active_identity_!=fingerprint;
        EnsureLocked(model_id,config,fingerprint);
        result.timings.load_call_delta=needsLoad ? 1 : 0; result.timings.model_load_ms=Ms(checkpoint);
        checkpoint=Clock::now(); result.audio=std::make_shared<AudioBuffer>(engine_->Synthesize(effective));
        result.timings.synth_ms=Ms(checkpoint); result.timings.synth_call_delta=1; result.timings.source="synth";
        AudioCache::Validate(*result.audio);
        checkpoint=Clock::now();
        if (cache_ && !clearAtStart && !token.stop_requested()) cache_->Put(result.timings.key,fingerprint,result.audio,epoch);
        result.timings.cache_write_ms=Ms(checkpoint);
    }
    if (token.stop_requested()) throw std::runtime_error("TTS_CANCELED");
    result.timings.audio_prepare_ms=Ms(start);
    return result;
}
void TtsService::Unload() noexcept {
    std::lock_guard lock(mutex_); if (engine_) engine_->Unload(); active_model_id_.clear(); active_identity_.clear();
}
std::string TtsService::ActiveEngineId() const { std::lock_guard lock(mutex_); return engine_ ? engine_->Id() : std::string{}; }
std::string TtsService::ActiveModelId() const { std::lock_guard lock(mutex_); return active_model_id_; }
} // namespace adayo
