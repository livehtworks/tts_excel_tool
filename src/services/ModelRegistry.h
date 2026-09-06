#pragma once

#include "core/domain/Types.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace adayo {

struct TtsModelEntry {
    std::string id;
    std::string display_name;
    std::filesystem::path root;
    TtsModelConfig config;
    std::vector<std::pair<int, std::string>> speakers;
};

struct TtsModelDiagnostic {
    std::filesystem::path path;
    std::string id;
    std::string error;
};

struct ModelRegistryScanResult {
    std::vector<TtsModelEntry> entries;
    std::vector<TtsModelDiagnostic> invalid;
};

class ModelRegistry {
public:
    explicit ModelRegistry(std::filesystem::path models_root);

    std::vector<TtsModelEntry> ScanSherpaModels() const;
    ModelRegistryScanResult ScanSherpaModelsWithDiagnostics() const;
    static TtsModelEntry LoadModelJson(const std::filesystem::path& model_json);

private:
    std::filesystem::path models_root_;
};

} // namespace adayo
