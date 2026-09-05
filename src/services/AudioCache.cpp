#include "services/AudioCache.h"
#include "platform/FileIo.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <fstream>
#include <map>
#include <mutex>
#include <regex>
#include <stdexcept>
#ifdef ADAYO_HAS_JSON_CONFIG
#include <nlohmann/json.hpp>
#endif
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace adayo {
namespace {
std::uint64_t Now() {
    static std::atomic<std::uint64_t> last{};
    const auto now=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    auto previous=last.load();
    for(;;) { const auto next=(std::max)(now,previous+1); if(last.compare_exchange_weak(previous,next)) return next; }
}
bool KeyValid(const std::string& key) {
    return key.size() == 64 && key.find_first_not_of("0123456789abcdef") == std::string::npos;
}
void CheckOptions(const AudioCacheOptions& value) {
    if (!value.memory_limit_bytes || value.memory_limit_bytes > 64ull * 1024 * 1024 ||
        !value.disk_limit_bytes || value.disk_limit_bytes > 16384ull * 1024 * 1024 ||
        !value.entry_limit || value.entry_limit > 20000) throw std::invalid_argument("Invalid bounded audio cache options");
}
void U32(std::string& data, std::uint32_t value, unsigned count = 4) {
    for (unsigned i = 0; i < count; ++i) data.push_back(static_cast<char>(value >> (8 * i)));
}
std::uint32_t Read32(std::string_view data, std::size_t at, unsigned count = 4) {
    if (at + count > data.size()) throw std::runtime_error("Truncated WAV header");
    std::uint32_t value = 0;
    for (unsigned i = 0; i < count; ++i) value |= std::uint32_t(static_cast<unsigned char>(data[at+i])) << (8*i);
    return value;
}
std::string Wav(const AudioBuffer& audio) {
    AudioCache::Validate(audio);
    if (audio.samples.size() > (UINT32_MAX - 36) / 4) throw std::runtime_error("WAV size exceeds RIFF limit");
    const auto bytes = static_cast<std::uint32_t>(audio.samples.size() * 4);
    std::string data = "RIFF"; U32(data, 36 + bytes); data += "WAVEfmt "; U32(data,16);
    U32(data,3,2); U32(data,audio.channels,2); U32(data,audio.sample_rate);
    U32(data,audio.sample_rate * audio.channels * 4); U32(data,audio.channels*4,2); U32(data,32,2);
    data += "data"; U32(data,bytes);
    data.reserve(44ull + bytes);
    for (float sample : audio.samples) U32(data,std::bit_cast<std::uint32_t>(sample));
    return data;
}
}

struct AudioCache::Impl {
    struct Entry {
        std::uint64_t bytes{}, used{};
        std::string fingerprint;
        std::shared_ptr<const AudioBuffer> memory;
        std::weak_ptr<const AudioBuffer> lease;
    };
    std::filesystem::path root;
    AudioCacheOptions options;
    // io serializes disk operations; state is short-held and never spans disk IO.
    mutable std::mutex state;
    std::mutex io;
    std::map<std::string,Entry> entries;
    AudioCacheStats stats;
#ifdef _WIN32
    HANDLE ownership{INVALID_HANDLE_VALUE};
#endif
    bool Pinned(const Entry& entry) const {
        const auto lease=entry.lease.lock();
        if(!lease) return false;
        long owned=1;
        for(const auto& [key,candidate]:entries) if(candidate.memory.get()==lease.get()) ++owned;
        return lease.use_count()>owned;
    }
    void Warn(std::string warning) { std::lock_guard lock(state); stats.warning = std::move(warning); }
    bool Remove(const std::string& key) {
        bool ok = true;
        for (const char* suffix : {".json", ".wav"}) {
            const auto path = root / (key + suffix);
            try { RequireOrdinaryPath(path); std::error_code ec; std::filesystem::remove(path,ec); if (ec) ok = false; }
            catch (...) { ok = false; }
        }
        return ok;
    }
    void Recount() {
        std::uint64_t bytes = 0;
        if (stats.disk_enabled) {
            for (const auto& file : std::filesystem::directory_iterator(root)) {
                RequireOrdinaryPath(file.path());
                if (file.is_regular_file()) bytes += file.file_size();
            }
        }
        std::lock_guard lock(state);
        stats.used_bytes = bytes;
        stats.entries = entries.size();
        stats.memory_bytes = 0;
        for (const auto& [key, entry] : entries) if (entry.memory) stats.memory_bytes += entry.memory->samples.size()*sizeof(float);
    }
    bool MakeRoom(std::uint64_t bytes, std::size_t slots, const AudioCacheOptions& limits) {
        Recount();
        for (;;) {
            std::string victim;
            {
                std::lock_guard lock(state);
                if (stats.used_bytes <= limits.disk_limit_bytes && bytes <= limits.disk_limit_bytes - stats.used_bytes && entries.size()+slots <= limits.entry_limit) return true;
                auto oldest = UINT64_MAX;
                for (const auto& [key, entry] : entries) if (!Pinned(entry) && entry.used < oldest) { oldest=entry.used; victim=key; }
            }
            if (victim.empty() || !Remove(victim)) return false;
            { std::lock_guard lock(state); entries.erase(victim); ++stats.evicted; }
            Recount();
        }
    }
    void TrimMemory() {
        auto bytes = std::uint64_t{};
        for (const auto& [key,entry] : entries) if (entry.memory) bytes += entry.memory->samples.size()*4;
        while (bytes > options.memory_limit_bytes) {
            auto victim = entries.end();
            for (auto it=entries.begin(); it!=entries.end(); ++it) if (it->second.memory &&
                (victim==entries.end() || it->second.used < victim->second.used)) victim=it;
            if (victim==entries.end()) break;
            bytes -= victim->second.memory->samples.size()*4;
            victim->second.memory.reset();
        }
        stats.memory_bytes=bytes;
    }
};

AudioCache::AudioCache(std::filesystem::path root, AudioCacheOptions options) : impl_(std::make_unique<Impl>()) {
    CheckOptions(options);
    impl_->root = std::filesystem::absolute(root).lexically_normal();
    impl_->options = options;
    impl_->stats.active_limit = options.disk_limit_bytes;
#if defined(ADAYO_HAS_JSON_CONFIG) && defined(_WIN32)
    try {
        RequireOrdinaryPath(impl_->root);
        const auto marker = impl_->root / "owner.json";
        if (std::filesystem::exists(impl_->root) && !std::filesystem::exists(marker) && !std::filesystem::is_empty(impl_->root))
            throw std::runtime_error("Cache directory ownership is unknown");
        std::filesystem::create_directories(impl_->root);
        impl_->ownership = CreateFileW((impl_->root / "owner.lock").c_str(), GENERIC_READ|GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (impl_->ownership == INVALID_HANDLE_VALUE) throw std::runtime_error("Disk cache is owned by another process or is not writable");
        if (std::filesystem::exists(marker)) {
            RequireOrdinaryPath(marker);
            std::ifstream input(marker);
            const auto document=nlohmann::json::parse(input);
            if (document.value("application_id","")!="AdayoCorpusTool" || document.value("schema",0)!=1) throw std::runtime_error("Cache ownership marker mismatch");
        } else {
            const std::string value=R"({"application_id":"AdayoCorpusTool","schema":1})";
            WriteBinaryFileAtomically(marker,value.data(),value.size());
        }
        impl_->stats.disk_enabled=true;
        const std::regex temp("^[0-9a-f]{64}\\.(wav|json)\\.tmp-[0-9-]+$");
        for (const auto& file : std::filesystem::directory_iterator(impl_->root)) {
            RequireOrdinaryPath(file.path());
            const auto name=file.path().filename().string();
            if (std::regex_match(name,temp)) { std::filesystem::remove(file.path()); continue; }
            const auto key=file.path().stem().string();
            if (!KeyValid(key)) continue;
            if (file.path().extension()==".wav" && !std::filesystem::exists(impl_->root/(key+".json"))) { impl_->Remove(key); continue; }
            if (file.path().extension()!=".json") continue;
            try {
                if (file.file_size()>16384) throw std::runtime_error("Cache metadata exceeds limit");
                std::ifstream input(file.path()); const auto j=nlohmann::json::parse(input);
                if (j.at("key").get<std::string>()!=key || j.at("schema").get<int>()!=1) throw std::runtime_error("Invalid cache metadata");
                auto wav=impl_->root/(key+".wav"); RequireOrdinaryPath(wav);
                if (!std::filesystem::is_regular_file(wav) || j.at("bytes").get<std::uint64_t>()!=std::filesystem::file_size(wav)) throw std::runtime_error("Invalid cache size");
                impl_->entries[key]={std::filesystem::file_size(wav)+file.file_size(),j.at("used").get<std::uint64_t>(),j.at("fingerprint").get<std::string>(),{}, {}};
            } catch (const std::exception& ex) { impl_->Warn(ex.what()); impl_->Remove(key); }
        }
        if (!impl_->MakeRoom(0,0,options)) throw std::runtime_error("Cache cannot meet configured quota");
    } catch (const std::exception& ex) {
        impl_->stats.disk_enabled=false;
        impl_->Warn(ex.what());
    }
#else
    impl_->Warn("Persistent cache unsupported without Windows and JSON configuration support");
#endif
}
AudioCache::~AudioCache() {
#ifdef _WIN32
    if (impl_->ownership!=INVALID_HANDLE_VALUE) CloseHandle(impl_->ownership);
#endif
}
void AudioCache::Validate(const AudioBuffer& audio) {
    if (audio.samples.empty() || audio.sample_rate<8000 || audio.sample_rate>384000 || audio.channels<1 || audio.channels>8 ||
        audio.samples.size()%audio.channels || !std::all_of(audio.samples.begin(),audio.samples.end(),[](float v){return std::isfinite(v);}))
        throw std::runtime_error("Invalid audio samples/rate/channels");
}
AudioCacheStats AudioCache::Stats() const { std::lock_guard lock(impl_->state); return impl_->stats; }

AudioCacheHit AudioCache::Get(const std::string& key, const std::string& fingerprint, std::uint64_t epoch) {
    if (!KeyValid(key)) throw std::invalid_argument("Invalid cache key");
    std::lock_guard io(impl_->io);
    {
        std::lock_guard lock(impl_->state);
        if (!impl_->options.enabled || impl_->stats.clearing || impl_->stats.epoch!=epoch) return {};
        auto it=impl_->entries.find(key);
        if (it==impl_->entries.end() || it->second.fingerprint!=fingerprint) return {};
        it->second.used=Now();
        if (it->second.memory) return {it->second.memory,"memory"};
        if (!impl_->stats.disk_enabled) return {};
    }
#ifdef ADAYO_HAS_JSON_CONFIG
    try {
        const auto meta=impl_->root/(key+".json"), wav=impl_->root/(key+".wav");
        RequireOrdinaryPath(meta); RequireOrdinaryPath(wav);
        if (std::filesystem::file_size(meta)>16384) throw std::runtime_error("Cache metadata too large");
        std::ifstream input(meta); auto j=nlohmann::json::parse(input); input.close();
        const auto bytes=std::filesystem::file_size(wav);
        if (j.at("schema").get<int>()!=1 || j.at("key").get<std::string>()!=key || j.at("fingerprint").get<std::string>()!=fingerprint || j.at("bytes").get<std::uint64_t>()!=bytes || bytes<44 || bytes>impl_->options.disk_limit_bytes)
            throw std::runtime_error("Cache metadata mismatch");
        std::ifstream stream(wav,std::ios::binary);
        std::string header(44,'\0'); stream.read(header.data(),44);
        const auto count=Read32(header,40);
        const auto channels=Read32(header,22,2), rate=Read32(header,24);
        if (!stream || header.substr(0,4)!="RIFF" || header.substr(8,8)!="WAVEfmt " || header.substr(36,4)!="data" ||
            Read32(header,4)!=bytes-8 || Read32(header,16)!=16 || Read32(header,20,2)!=3 || Read32(header,34,2)!=32 ||
            count!=bytes-44 || count==0 || count%4 || channels<1 || channels>8 || rate<8000 || rate>384000 ||
            count%(channels*4) || Read32(header,28)!=rate*channels*4 || Read32(header,32,2)!=channels*4 ||
            j.at("frames").get<std::uint64_t>()!=count/(channels*4) || j.at("sample_rate").get<unsigned>()!=rate || j.at("channels").get<unsigned>()!=channels)
            throw std::runtime_error("Invalid cache WAV header");
        if (FileSha256(wav)!=j.at("audio_hash").get<std::string>()) throw std::runtime_error("Cache WAV hash mismatch");
        auto audio=std::make_shared<AudioBuffer>(); audio->sample_rate=rate; audio->channels=channels;
        audio->samples.resize(count/4);
        std::array<char,4> sample{};
        for (auto& value:audio->samples) { stream.read(sample.data(),4); value=std::bit_cast<float>(Read32(std::string_view(sample.data(),4),0)); }
        if (!stream) throw std::runtime_error("Truncated cache PCM");
        Validate(*audio);
        { std::lock_guard lock(impl_->state); impl_->entries[key].lease=audio; }
        j["used"]=Now(); const auto updated=j.dump();
        if (impl_->MakeRoom(updated.size()+1024,0,impl_->options)) WriteBinaryFileAtomically(meta,updated.data(),updated.size());
        {
            std::lock_guard lock(impl_->state);
            if (impl_->stats.epoch!=epoch || impl_->stats.clearing) return {};
            auto& entry=impl_->entries[key]; entry.fingerprint=fingerprint; entry.memory=audio; entry.lease=audio; entry.used=Now(); impl_->TrimMemory();
        }
        return {audio,"disk"};
    } catch (const std::exception& ex) {
        impl_->Warn(std::string("Corrupt/unreadable audio cache entry: ")+ex.what());
        if (impl_->Remove(key)) { std::lock_guard lock(impl_->state); impl_->entries.erase(key); }
        impl_->Recount();
    }
#endif
    return {};
}

void AudioCache::Put(const std::string& key, const std::string& fingerprint, std::shared_ptr<const AudioBuffer> audio, std::uint64_t epoch) {
    Validate(*audio);
    if (!KeyValid(key)) throw std::invalid_argument("Invalid cache key");
    {
        std::lock_guard lock(impl_->state);
        if (!impl_->options.enabled || impl_->stats.clearing || impl_->stats.epoch!=epoch) { ++impl_->stats.skipped; return; }
    }
    std::lock_guard io(impl_->io);
    try {
        const auto sampleBytes=audio->samples.size()*4ull;
        if (sampleBytes+8192 > impl_->options.disk_limit_bytes) { std::lock_guard lock(impl_->state); ++impl_->stats.skipped; return; }
        if (!impl_->MakeRoom(sampleBytes+8192,1,impl_->options)) { impl_->Warn("Cache put skipped: quota or active leases"); std::lock_guard lock(impl_->state); ++impl_->stats.skipped; return; }
#ifdef ADAYO_HAS_JSON_CONFIG
        if (impl_->stats.disk_enabled) {
            const auto bytes=Wav(*audio);
            const auto stamp=Now();
            nlohmann::json j={{"schema",1},{"key",key},{"fingerprint",fingerprint},{"audio_hash",Sha256(bytes)},
                {"bytes",bytes.size()},{"frames",audio->samples.size()/audio->channels},{"sample_rate",audio->sample_rate},
                {"channels",audio->channels},{"created",stamp},{"used",stamp}};
            const auto metadata=j.dump();
            const auto wav=impl_->root/(key+".wav"), meta=impl_->root/(key+".json");
            RequireOrdinaryPath(wav); RequireOrdinaryPath(meta);
            WriteBinaryFileAtomically(wav,bytes.data(),bytes.size());
            WriteBinaryFileAtomically(meta,metadata.data(),metadata.size());
        }
#endif
        bool stale;
        {
            std::lock_guard lock(impl_->state);
            stale=impl_->stats.epoch!=epoch || impl_->stats.clearing;
            if (!stale) { impl_->entries[key]={sampleBytes+44,Now(),fingerprint,audio,audio}; impl_->TrimMemory(); impl_->stats.entries=impl_->entries.size(); }
        }
        if (stale && impl_->stats.disk_enabled) impl_->Remove(key);
        impl_->Recount();
    } catch (const std::exception& ex) { impl_->Warn(std::string("Audio cache write skipped: ")+ex.what()); std::lock_guard lock(impl_->state); ++impl_->stats.skipped; }
}
std::uint64_t AudioCache::BeginClear() {
    std::lock_guard lock(impl_->state);
    ++impl_->stats.epoch;
    impl_->stats.clearing=true;
    for (auto& [key,entry]:impl_->entries) entry.memory.reset();
    impl_->stats.memory_bytes=0;
    return impl_->stats.epoch;
}
AudioCacheStats AudioCache::FinishClear() {
    std::lock_guard io(impl_->io);
    std::vector<std::string> keys;
    { std::lock_guard lock(impl_->state); for (const auto& [key,entry]:impl_->entries) keys.push_back(key); impl_->stats.deleted_entries=0; impl_->stats.failed_entries=0; impl_->stats.deleted_bytes=0; }
    for (const auto& key:keys) {
        bool removed=!impl_->stats.disk_enabled || impl_->Remove(key);
        std::lock_guard lock(impl_->state);
        if (removed) { impl_->stats.deleted_bytes+=impl_->entries[key].bytes; ++impl_->stats.deleted_entries; impl_->entries.erase(key); }
        else ++impl_->stats.failed_entries;
    }
    impl_->Recount();
    { std::lock_guard lock(impl_->state); impl_->stats.clearing=false; if (impl_->stats.failed_entries) impl_->stats.warning="Audio cache clear partially failed"; }
    return Stats();
}
void AudioCache::Configure(AudioCacheOptions options) {
    CheckOptions(options);
    std::lock_guard io(impl_->io);
    if (!impl_->MakeRoom(0,0,options)) { impl_->Warn("Cache shrink refused: pinned entries or removal failure; old quota remains active"); throw std::runtime_error(Stats().warning); }
    std::lock_guard lock(impl_->state);
    impl_->options=options; impl_->stats.active_limit=options.disk_limit_bytes; impl_->TrimMemory();
}
} // namespace adayo
