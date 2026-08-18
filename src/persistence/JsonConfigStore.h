#pragma once

#include "persistence/ConfigModel.h"

#include <filesystem>
#include <optional>
#include <string>

namespace adayo {

enum class ConfigLoadStatus {
    Loaded,
    Missing,
    CorruptBackedUp,
    FutureSchema,
};

struct ConfigLoadResult {
    AppConfig config;
    ConfigLoadStatus status{ConfigLoadStatus::Missing};
    std::optional<std::filesystem::path> backup_path;
    std::string message;
    bool allow_save{true};
};

class JsonConfigStore {
public:
    explicit JsonConfigStore(std::filesystem::path path);

    AppConfig Load() const;
    ConfigLoadResult LoadOrDefault() const;
    void Save(const AppConfig& config) const;

    const std::filesystem::path& Path() const { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace adayo
