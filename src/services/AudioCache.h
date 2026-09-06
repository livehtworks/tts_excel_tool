#pragma once
#include "core/domain/Types.h"
#include <filesystem>
#include <memory>
#include <string>

namespace adayo {
struct AudioCacheOptions {
    bool enabled{true};
    std::uint64_t memory_limit_bytes{64ull * 1024 * 1024};
    std::uint64_t disk_limit_bytes{2048ull * 1024 * 1024};
    std::size_t entry_limit{20000};
};
struct AudioCacheStats {
    std::uint64_t used_bytes{}, memory_bytes{}, active_limit{}, epoch{}, evicted{}, skipped{};
    std::size_t entries{}, deleted_entries{}, failed_entries{};
    std::uint64_t deleted_bytes{};
    std::uint64_t metadata_writes{}, recounts{}, eviction_scans{}, lru_accesses{}, pin_scans{};
    std::uint64_t timestamp_write_failures{};
    std::uint64_t active_bytes{};
    std::size_t dirty_entries{};
    bool disk_enabled{}, clearing{}, accounting_valid{true};
    std::string warning;
};
struct AudioCacheHit {
    std::shared_ptr<const AudioBuffer> audio;
    std::string source;
};
class AudioCache {
public:
    AudioCache(std::filesystem::path root, AudioCacheOptions options);
    ~AudioCache();
    AudioCache(const AudioCache&) = delete;
    AudioCache& operator=(const AudioCache&) = delete;
    AudioCacheHit Get(const std::string& key, const std::string& fingerprint, std::uint64_t epoch);
    void Put(const std::string& key, const std::string& fingerprint,
        std::shared_ptr<const AudioBuffer> audio, std::uint64_t epoch);
    std::uint64_t BeginClear();
    AudioCacheStats FinishClear();
    void Configure(AudioCacheOptions options);
    AudioCacheStats Stats(bool include_active = false) const;
    void FlushUsage();
    static void Validate(const AudioBuffer& audio);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace adayo
