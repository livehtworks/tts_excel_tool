#pragma once

#include "core/domain/Types.h"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace adayo {

struct VoiceValidationObservation {
    std::string observed_at, run_id, model_sha256, frontend_rule_coverage, evidence_scope;
    std::vector<std::string> warnings;
    std::string Status(const std::string& current_model_sha256 = {}) const;
};

struct TtsModelEntry {
    std::string id;
    std::string display_name;
    std::filesystem::path root;
    TtsModelConfig config;
    std::vector<std::pair<int, std::string>> speakers;
    std::optional<VoiceValidationObservation> observation;
    std::string observation_error;
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
    static TtsModelEntry LoadModelJson(const std::filesystem::path& model_json, const std::string& offline_transaction = {});

private:
    std::filesystem::path models_root_;
};

} // namespace adayo
