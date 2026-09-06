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
#include <set>
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
        std::string metadata;
    };
    std::filesystem::path root;
    AudioCacheOptions options;
    // io serializes disk operations; state is short-held and never spans disk IO.
    mutable std::mutex state;
    std::mutex io;
    std::map<std::string,Entry> entries;
    std::set<std::pair<std::uint64_t,std::string>> lru, memory_lru;
    std::set<std::string> dirty;
    struct References { std::weak_ptr<const AudioBuffer> lease; std::size_t cached{}; };
    std::map<const AudioBuffer*,References> references;
    const AudioBuffer* reference_cursor{};
    std::map<std::filesystem::path,std::uint64_t> file_sizes;
    std::set<std::filesystem::path> unmanaged_files;
    std::filesystem::file_time_type directory_stamp{};
    std::size_t accesses_since_flush{};
    AudioCacheStats stats;
    mutable std::atomic<std::uint64_t> metadata_writes{}, recounts{}, eviction_scans{}, lru_accesses{}, pin_scans{};
    bool owned_disk{false};
#ifdef _WIN32
    HANDLE ownership{INVALID_HANDLE_VALUE};
#endif
    bool Pinned(const Entry& entry) const {
        ++pin_scans;
        const auto lease=entry.lease.lock();
        if(!lease) return false;
        const auto it=references.find(lease.get());
        const auto owned=it==references.end()?0:it->second.cached;
        return lease.use_count()>static_cast<long>(owned+1);
    }
    void SetMemory(const std::string& key,Entry& entry,std::shared_ptr<const AudioBuffer> audio) {
        const auto previous=entry.memory.get();
        if(entry.memory) {
            memory_lru.erase({entry.used,key});
            auto& refs=references.at(entry.memory.get());
            if(--refs.cached==0) stats.memory_bytes-=entry.memory->samples.size()*sizeof(float);
        }
        entry.memory=std::move(audio);
        if(previous) {
            const auto found=references.find(previous);
            if(found!=references.end() && found->second.lease.expired()) references.erase(found);
        }
        if(entry.memory) {
            auto& refs=references[entry.memory.get()]; refs.lease=entry.memory;
            if(refs.cached++==0) stats.memory_bytes+=entry.memory->samples.size()*sizeof(float);
            entry.lease=entry.memory;
            memory_lru.emplace(entry.used,key);
        }
        for(int i=0;i<2 && !references.empty();++i) {
            auto it=references.upper_bound(reference_cursor);
            if(it==references.end()) it=references.begin();
            reference_cursor=it->first;
            if(it->second.lease.expired()) references.erase(it);
        }
    }
    void Touch(const std::string& key,Entry& entry) {
        lru.erase({entry.used,key}); memory_lru.erase({entry.used,key});
        entry.used=Now(); lru.emplace(entry.used,key);
        if(entry.memory) memory_lru.emplace(entry.used,key);
        if(stats.disk_enabled && !entry.metadata.empty()) dirty.insert(key);
    }
    void Erase(const std::string& key) {
        const auto it=entries.find(key);
        if(it==entries.end()) return;
        SetMemory(key,it->second,{});
        lru.erase({it->second.used,key}); dirty.erase(key); entries.erase(it);
        stats.entries=entries.size();
    }
    void AccountFile(const std::filesystem::path& path) {
        RequireOrdinaryPath(path);
        const auto exists=std::filesystem::exists(path);
        const auto size=exists?std::filesystem::file_size(path):0;
        std::lock_guard lock(state);
        auto& old=file_sizes[path]; stats.used_bytes=stats.used_bytes-old+size; old=size;
        if(!exists) file_sizes.erase(path);
    }
    void RememberDirectory() { if(owned_disk) directory_stamp=std::filesystem::last_write_time(root); }
    void CheckDirectory() {
        if(owned_disk && std::filesystem::last_write_time(root)!=directory_stamp) Recount();
        for(const auto& path:unmanaged_files) {
            RequireOrdinaryPath(path);
            if(!std::filesystem::exists(path) || std::filesystem::file_size(path)!=file_sizes.at(path)) { Recount(); break; }
        }
    }
    void Warn(std::string warning) { std::lock_guard lock(state); stats.warning = std::move(warning); }
    bool Remove(const std::string& key) {
        bool ok = true;
        for (const char* suffix : {".json", ".wav"}) {
            const auto path = root / (key + suffix);
            try { RequireOrdinaryPath(path); std::error_code ec; std::filesystem::remove(path,ec); if (ec) ok = false; AccountFile(path); }
            catch (...) { ok = false; }
        }
        if(!ok) Recount();
        RememberDirectory(); return ok;
    }
    void Recount() {
        ++recounts;
        std::uint64_t bytes = 0;
        std::map<std::filesystem::path,std::uint64_t> sizes;
        try { if (owned_disk) {
            for (const auto& file : std::filesystem::directory_iterator(root)) {
                RequireOrdinaryPath(file.path());
                if (file.is_regular_file()) { const auto size=file.file_size(); bytes+=size; sizes[file.path()]=size; }
            }
        } } catch(const std::exception& ex) {
            std::lock_guard lock(state); stats.accounting_valid=false;
            stats.warning=std::string("Cache occupancy cannot be verified: ")+ex.what();
            throw;
        }
        RememberDirectory();
        std::lock_guard lock(state);
        file_sizes=std::move(sizes);
        unmanaged_files.clear();
        for(const auto& [path,size]:file_sizes) {
            const auto key=path.stem().string();
            if(!KeyValid(key) || (path.extension()!=".wav" && path.extension()!=".json")) unmanaged_files.insert(path);
        }
        stats.accounting_valid=true;
        stats.used_bytes = bytes;
        stats.entries = entries.size();
    }
    bool MakeRoom(std::uint64_t bytes, std::size_t slots, const AudioCacheOptions& limits, const std::string& protected_key={}) {
        CheckDirectory();
        std::set<std::string> unavailable;
        for (;;) {
            std::string victim;
            {
                std::lock_guard lock(state);
                if(!stats.accounting_valid) return false;
                if (stats.used_bytes <= limits.disk_limit_bytes && bytes <= limits.disk_limit_bytes - stats.used_bytes && entries.size()+slots <= limits.entry_limit) return true;
                for (const auto& [stamp,key] : lru) {
                    ++eviction_scans;
                    if(key!=protected_key && !unavailable.contains(key) && !Pinned(entries.at(key))) { victim=key; break; }
                }
            }
            if (victim.empty()) return false;
            if(owned_disk && !Remove(victim)) { unavailable.insert(victim); continue; }
            { std::lock_guard lock(state); Erase(victim); ++stats.evicted; }
        }
    }
    void TrimMemory() {
        while (stats.memory_bytes>options.memory_limit_bytes && !memory_lru.empty()) {
            const auto key=memory_lru.begin()->second;
            SetMemory(key,entries.at(key),{});
        }
    }
    void TouchMetadata(const std::string& key,const std::string& bytes) {
        const auto path=root/(key+".json");
        RequireOrdinaryPath(path);
        if(!std::filesystem::is_regular_file(path)) throw std::runtime_error("Cache metadata disappeared");
        CheckDirectory();
        if(!file_sizes.contains(path) || file_sizes.at(path)!=std::filesystem::file_size(path)) Recount();
        bool room;
        {
            std::lock_guard lock(state);
            room=stats.accounting_valid && stats.used_bytes<=options.disk_limit_bytes && bytes.size()+1024<=options.disk_limit_bytes-stats.used_bytes;
        }
        if(!room && !MakeRoom(bytes.size()+1024,0,options,key)) throw std::runtime_error("Cache LRU timestamp skipped: quota");
        WriteBinaryFileAtomically(path,bytes.data(),bytes.size());
        ++metadata_writes;
        AccountFile(path); RememberDirectory();
    }
    void FlushDirty(bool drain) {
#ifdef ADAYO_HAS_JSON_CONFIG
        std::vector<std::string> pending;
        {
            std::lock_guard lock(state);
            if(stats.clearing || !stats.disk_enabled) return;
            // Keep synchronous hit-path IO small; sequence/shutdown drains retain all records.
            const auto count=drain?dirty.size():(std::min)(dirty.size(),std::size_t{2});
            auto end=dirty.begin(); std::advance(end,count);
            pending.assign(dirty.begin(),end); dirty.erase(dirty.begin(),end);
            accesses_since_flush=0;
        }
        // Snapshot once: failed records are not retried within this drain.
        for(std::size_t offset=0;offset<pending.size();offset+=32) {
            for(std::size_t i=offset;i<(std::min)(offset+32,pending.size());++i) {
                const auto& key=pending[i];
                try {
                    std::string metadata; std::uint64_t used;
                    { std::lock_guard lock(state);
                      auto it=entries.find(key); if(stats.clearing || it==entries.end()) continue;
                      metadata=it->second.metadata; used=it->second.used; }
                    auto j=nlohmann::json::parse(metadata); j["used"]=used;
                    const auto bytes=j.dump(); TouchMetadata(key,bytes);
                    std::lock_guard lock(state);
                    if(auto it=entries.find(key);it!=entries.end()) it->second.metadata=bytes;
                } catch(const std::exception& ex) {
                    try { Recount(); } catch(const std::exception&) {}
                    std::lock_guard lock(state); ++stats.timestamp_write_failures;
                    stats.warning=std::string("Cache LRU timestamp update failed: ")+ex.what();
                }
            }
        }
#endif
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
        impl_->owned_disk=true;
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
                impl_->entries[key]={std::filesystem::file_size(wav)+file.file_size(),j.at("used").get<std::uint64_t>(),j.at("fingerprint").get<std::string>(),{}, {},j.dump()};
                impl_->lru.emplace(impl_->entries.at(key).used,key);
            } catch (const std::exception& ex) { impl_->Warn(ex.what()); impl_->Remove(key); }
        }
        impl_->Recount();
        if (!impl_->MakeRoom(0,0,options)) throw std::runtime_error("Cache cannot meet configured quota");
    } catch (const std::exception& ex) {
        impl_->stats.disk_enabled=false;
        impl_->Warn(ex.what());
        try { impl_->Recount(); } catch (const std::exception& recount) { impl_->Warn(recount.what()); }
    }
#else
    impl_->Warn("Persistent cache unsupported without Windows and JSON configuration support");
#endif
}
AudioCache::~AudioCache() {
    FlushUsage();
#ifdef _WIN32
    if (impl_->ownership!=INVALID_HANDLE_VALUE) CloseHandle(impl_->ownership);
#endif
}
void AudioCache::Validate(const AudioBuffer& audio) {
    if (audio.samples.empty() || audio.sample_rate<8000 || audio.sample_rate>384000 || audio.channels<1 || audio.channels>8 ||
        audio.samples.size()%audio.channels || !std::all_of(audio.samples.begin(),audio.samples.end(),[](float v){return std::isfinite(v);}))
        throw std::runtime_error("Invalid audio samples/rate/channels");
}
AudioCacheStats AudioCache::Stats(bool include_active) const {
    std::lock_guard lock(impl_->state);
    auto result=impl_->stats;
    result.metadata_writes=impl_->metadata_writes.load(); result.recounts=impl_->recounts.load();
    result.eviction_scans=impl_->eviction_scans.load(); result.lru_accesses=impl_->lru_accesses.load();
    result.pin_scans=impl_->pin_scans.load(); result.dirty_entries=impl_->dirty.size();
    if(include_active) {
        for(auto it=impl_->references.begin();it!=impl_->references.end();) {
            const auto lease=it->second.lease.lock();
            if(!lease) { it=impl_->references.erase(it); continue; }
            if(lease.use_count()>static_cast<long>(it->second.cached+1)) result.active_bytes+=lease->samples.size()*sizeof(float);
            ++it;
        }
    }
    return result;
}
void AudioCache::FlushUsage() {
    std::lock_guard io(impl_->io);
    impl_->FlushDirty(true);
}

AudioCacheHit AudioCache::Get(const std::string& key, const std::string& fingerprint, std::uint64_t epoch) {
    ++impl_->lru_accesses;
    if (!KeyValid(key)) throw std::invalid_argument("Invalid cache key");
    std::lock_guard io(impl_->io);
    std::shared_ptr<const AudioBuffer> memory_hit;
    {
        std::lock_guard lock(impl_->state);
        if (!impl_->options.enabled || impl_->stats.clearing || impl_->stats.epoch!=epoch) return {};
        auto it=impl_->entries.find(key);
        if (it==impl_->entries.end() || it->second.fingerprint!=fingerprint) return {};
        impl_->Touch(key,it->second);
        memory_hit=it->second.memory;
        if (!memory_hit && !impl_->stats.disk_enabled) return {};
    }
    if(memory_hit) {
        if(++impl_->accesses_since_flush>=32) impl_->FlushDirty(false);
        return {memory_hit,"memory"};
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
        {
            std::lock_guard lock(impl_->state);
            if (impl_->stats.epoch!=epoch || impl_->stats.clearing) return {};
            impl_->entries.at(key).lease=audio;
        }
        {
            std::lock_guard lock(impl_->state);
            if (impl_->stats.epoch!=epoch || impl_->stats.clearing) return {};
            auto& entry=impl_->entries.at(key); entry.metadata=j.dump();
            impl_->SetMemory(key,entry,audio); impl_->Touch(key,entry); impl_->TrimMemory();
        }
        if(++impl_->accesses_since_flush>=32) impl_->FlushDirty(false);
        return {audio,"disk"};
    } catch (const std::exception& ex) {
        impl_->Warn(std::string("Corrupt/unreadable audio cache entry: ")+ex.what());
        if (impl_->Remove(key)) { std::lock_guard lock(impl_->state); impl_->Erase(key); }
        try { impl_->Recount(); } catch (const std::exception& recount) { impl_->Warn(recount.what()); }
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
        { std::lock_guard lock(impl_->state);
          if(!impl_->options.enabled || impl_->stats.clearing || impl_->stats.epoch!=epoch) { ++impl_->stats.skipped; return; } }
        const auto sampleBytes=audio->samples.size()*4ull;
        if (sampleBytes+8192 > impl_->options.disk_limit_bytes) { std::lock_guard lock(impl_->state); ++impl_->stats.skipped; return; }
        std::size_t slots;
        { std::lock_guard lock(impl_->state); slots=impl_->entries.contains(key)?0:1; }
        if (!impl_->MakeRoom(sampleBytes+8192,slots,impl_->options,key)) { impl_->Warn("Cache put skipped: quota or active leases"); std::lock_guard lock(impl_->state); ++impl_->stats.skipped; return; }
        std::string metadata;
        std::uint64_t entry_bytes=0;
#ifdef ADAYO_HAS_JSON_CONFIG
        if (impl_->stats.disk_enabled) {
            const auto bytes=Wav(*audio);
            const auto stamp=Now();
            nlohmann::json j={{"schema",1},{"key",key},{"fingerprint",fingerprint},{"audio_hash",Sha256(bytes)},
                {"bytes",bytes.size()},{"frames",audio->samples.size()/audio->channels},{"sample_rate",audio->sample_rate},
                {"channels",audio->channels},{"created",stamp},{"used",stamp}};
            metadata=j.dump();
            entry_bytes=bytes.size()+metadata.size();
            const auto wav=impl_->root/(key+".wav"), meta=impl_->root/(key+".json");
            RequireOrdinaryPath(wav); RequireOrdinaryPath(meta);
            WriteBinaryFileAtomically(wav,bytes.data(),bytes.size());
            impl_->AccountFile(wav);
            WriteBinaryFileAtomically(meta,metadata.data(),metadata.size());
            impl_->AccountFile(meta); impl_->RememberDirectory();
            ++impl_->metadata_writes;
        }
#endif
        bool stale;
        {
            std::lock_guard lock(impl_->state);
            stale=impl_->stats.epoch!=epoch || impl_->stats.clearing;
            if (!stale) {
                impl_->Erase(key);
                auto& entry=impl_->entries[key]; entry.bytes=entry_bytes; entry.used=Now(); entry.fingerprint=fingerprint; entry.metadata=std::move(metadata);
                impl_->lru.emplace(entry.used,key); impl_->SetMemory(key,entry,audio);
                impl_->TrimMemory(); impl_->stats.entries=impl_->entries.size();
            }
        }
        if (stale && impl_->stats.disk_enabled) impl_->Remove(key);
    } catch (const std::exception& ex) {
        impl_->Warn(std::string("Audio cache write skipped: ")+ex.what());
        try { impl_->Recount(); } catch (const std::exception& recount) { impl_->Warn(recount.what()); }
        std::lock_guard lock(impl_->state); ++impl_->stats.skipped;
    }
}
std::uint64_t AudioCache::BeginClear() {
    std::lock_guard lock(impl_->state);
    ++impl_->stats.epoch;
    impl_->stats.clearing=true;
    for (auto& [key,entry]:impl_->entries) impl_->SetMemory(key,entry,{});
    impl_->dirty.clear();
    impl_->stats.memory_bytes=0;
    return impl_->stats.epoch;
}
AudioCacheStats AudioCache::FinishClear() {
    std::lock_guard io(impl_->io);
    try {
    std::set<std::string> keys;
    { std::lock_guard lock(impl_->state); for (const auto& [key,entry]:impl_->entries) keys.insert(key); impl_->stats.deleted_entries=0; impl_->stats.failed_entries=0; impl_->stats.deleted_bytes=0; }
    if(impl_->owned_disk) {
        for(const auto& file:std::filesystem::directory_iterator(impl_->root)) {
            const auto key=file.path().stem().string();
            if(KeyValid(key) && (file.path().extension()==".json" || file.path().extension()==".wav")) keys.insert(key);
        }
    }
    for (const auto& key:keys) {
        bool removed=!impl_->owned_disk || impl_->Remove(key);
        std::lock_guard lock(impl_->state);
        if (removed) {
            if(auto entry=impl_->entries.find(key);entry!=impl_->entries.end()) impl_->stats.deleted_bytes+=entry->second.bytes;
            ++impl_->stats.deleted_entries; impl_->Erase(key);
        }
        else ++impl_->stats.failed_entries;
    }
    impl_->Recount();
    { std::lock_guard lock(impl_->state); impl_->stats.clearing=false; if (impl_->stats.failed_entries) impl_->stats.warning="Audio cache clear partially failed"; }
    } catch(const std::exception& ex) {
        std::lock_guard lock(impl_->state);
        impl_->stats.clearing=false; ++impl_->stats.failed_entries;
        impl_->stats.accounting_valid=false;
        impl_->stats.warning=std::string("Audio cache clear failed; occupancy is unverified: ")+ex.what();
    }
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
