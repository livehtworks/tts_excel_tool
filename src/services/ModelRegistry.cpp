#include "services/ModelRegistry.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <utility>

#include <nlohmann/json.hpp>

namespace adayo {
namespace {
using nlohmann::json;

std::string RequiredString(const json& j, const char* key, const std::filesystem::path& source) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_string() || it->get<std::string>().empty()) {
        throw std::runtime_error("模型配置缺少字段 " + std::string(key) + ": " + source.string());
    }
    return it->get<std::string>();
}

std::string OptionalString(const json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

std::filesystem::path Resolve(const std::filesystem::path& root, const std::string& value) {
    if (value.empty()) return {};
    std::filesystem::path p(value);
    return p.is_absolute() ? p : root / p;
}

std::string PathString(const std::filesystem::path& path) {
    if (path.empty()) return {};
    const auto u8 = path.u8string();
    return {u8.begin(), u8.end()};
}

std::string ResolveList(const std::filesystem::path& root, const std::string& values) {
    std::stringstream input(values);
    std::string item;
    std::string output;
    while (std::getline(input, item, ',')) {
        if (item.empty()) continue;
        if (!output.empty()) output.push_back(',');
        output += PathString(Resolve(root, item));
    }
    return output;
}

void RequireFile(const std::filesystem::path& path, const char* label) {
    if (path.empty()) return;
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("模型缺失 " + std::string(label) + ": " + PathString(path));
    }
}

void RequireFileList(const std::string& values, const char* label) {
    std::stringstream input(values);
    std::string item;
    while (std::getline(input, item, ',')) {
        if (item.empty()) continue;
        RequireFile(item, label);
    }
}
} // namespace

ModelRegistry::ModelRegistry(std::filesystem::path models_root)
    : models_root_(std::move(models_root)) {}

std::vector<TtsModelEntry> ModelRegistry::ScanSherpaModels() const {
    return ScanSherpaModelsWithDiagnostics().entries;
}

ModelRegistryScanResult ModelRegistry::ScanSherpaModelsWithDiagnostics() const {
    ModelRegistryScanResult result;
    std::vector<std::filesystem::path> model_files;
    std::vector<TtsModelDiagnostic> missing_json_dirs;
    if (!std::filesystem::exists(models_root_)) {
        return result;
    }
    for (const auto& dir : std::filesystem::directory_iterator(models_root_)) {
        if (!dir.is_directory()) continue;
        const auto model_json = dir.path() / "model.json";
        if (!std::filesystem::exists(model_json)) {
            missing_json_dirs.push_back({dir.path(), dir.path().filename().string(), "缺少 model.json"});
            continue;
        }
        model_files.push_back(model_json);
    }
    std::sort(model_files.begin(), model_files.end(), [](const auto& left, const auto& right) {
        return left.generic_u8string() < right.generic_u8string();
    });
    for (const auto& model_json : model_files) {
        try {
            result.entries.push_back(LoadModelJson(model_json));
        } catch (const std::exception& ex) {
            result.invalid.push_back({model_json.parent_path(), model_json.parent_path().filename().string(), ex.what()});
        }
    }
    std::sort(result.entries.begin(), result.entries.end(), [](const auto& left, const auto& right) {
        return left.id < right.id;
    });
    std::sort(missing_json_dirs.begin(), missing_json_dirs.end(), [](const auto& left, const auto& right) {
        return left.path.generic_u8string() < right.path.generic_u8string();
    });
    result.invalid.insert(result.invalid.end(), missing_json_dirs.begin(), missing_json_dirs.end());
    return result;
}

TtsModelEntry ModelRegistry::LoadModelJson(const std::filesystem::path& model_json) {
    std::ifstream input(model_json, std::ios::binary);
    if (!input) {
        throw std::runtime_error("无法读取模型配置: " + model_json.string());
    }
    json j;
    input >> j;

    const auto root = model_json.parent_path();
    TtsModelEntry entry;
    entry.root = root;
    entry.id = RequiredString(j, "id", model_json);
    entry.display_name = j.value("display_name", entry.id);

    auto& c = entry.config;
    c.engine_id = j.value("engine_id", std::string{"sherpa-vits"});
    c.model_path = PathString(Resolve(root, RequiredString(j, "model", model_json)));
    c.tokens_path = PathString(Resolve(root, RequiredString(j, "tokens", model_json)));
    c.data_dir = PathString(Resolve(root, OptionalString(j, "data_dir")));
    c.lexicon_path = PathString(Resolve(root, OptionalString(j, "lexicon")));
    c.rule_fsts = ResolveList(root, OptionalString(j, "rule_fsts"));
    c.language_code = j.value("language_code", std::string{});
    c.speaker_id = j.value("speaker_id", 0);
    c.num_threads = j.value("num_threads", 2);

    RequireFile(c.model_path, "model");
    RequireFile(c.tokens_path, "tokens");
    if (!c.data_dir.empty() && !std::filesystem::exists(c.data_dir)) {
        throw std::runtime_error("模型缺失 data_dir: " + c.data_dir);
    }
    RequireFile(c.lexicon_path, "lexicon");
    RequireFileList(c.rule_fsts, "rule_fsts");
    return entry;
}

} // namespace adayo
