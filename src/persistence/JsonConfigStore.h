#pragma once

#include "persistence/ConfigModel.h"

#include <filesystem>

namespace adayo {

class JsonConfigStore {
public:
    explicit JsonConfigStore(std::filesystem::path path);

    AppConfig Load() const;
    void Save(const AppConfig& config) const;

    const std::filesystem::path& Path() const { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace adayo
