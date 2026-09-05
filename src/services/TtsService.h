#pragma once

#include "core/tts/ITtsEngine.h"
#include "services/AudioCache.h"

#include <memory>
#include <mutex>
#include <string_view>
#include <map>
#include <stop_token>

namespace adayo {

struct TtsTimings {
    double key_build_ms{}, model_validation_ms{}, lookup_ms{}, model_load_ms{}, synth_ms{}, cache_read_ms{}, cache_write_ms{}, audio_prepare_ms{};
    std::uint64_t load_call_delta{}, synth_call_delta{};
    std::string key, source;
};
struct PreparedAudio { std::shared_ptr<const AudioBuffer> audio; TtsTimings timings; };

class TtsService {
public:
    void SetEngine(std::unique_ptr<ITtsEngine> engine);
    void EnsureModelLoaded(std::string_view model_id, const TtsModelConfig& config);
    AudioBuffer Synthesize(const TtsRequest& request);
    PreparedAudio Prepare(std::string_view model_id, const TtsModelConfig& config, const TtsRequest& request, std::stop_token token = {});
    void InitializeCache(const std::filesystem::path& root, AudioCacheOptions options);
    AudioCache* Cache() noexcept { return cache_.get(); }
    void Unload() noexcept;
    std::string ActiveEngineId() const;
    std::string ActiveModelId() const;

private:
    void EnsureLocked(std::string_view model_id, const TtsModelConfig& config, const std::string& identity);
    std::string ModelIdentity(const TtsModelConfig& config, bool require_assets);
    mutable std::timed_mutex mutex_;
    std::unique_ptr<ITtsEngine> engine_;
    std::string active_model_id_;
    std::string active_identity_;
    std::unique_ptr<AudioCache> cache_;
    struct Fingerprint { std::string snapshot, digest; };
    std::map<std::string, Fingerprint> fingerprints_;
};

} // namespace adayo
