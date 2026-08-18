#pragma once

#include "core/domain/Types.h"

#include <filesystem>
#include <string>
#include <vector>

namespace adayo {

struct TtsModelEntry {
    std::string id;
    std::string display_name;
    std::filesystem::path root;
    TtsModelConfig config;
};

class ModelRegistry {
public:
    explicit ModelRegistry(std::filesystem::path models_root);

    std::vector<TtsModelEntry> ScanSherpaModels() const;
    static TtsModelEntry LoadModelJson(const std::filesystem::path& model_json);

private:
    std::filesystem::path models_root_;
};

} // namespace adayo
