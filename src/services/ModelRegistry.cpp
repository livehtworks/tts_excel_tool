#include "services/ModelRegistry.h"

#include "platform/UnicodePath.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <system_error>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>

namespace adayo {
namespace {
using nlohmann::json;

std::string RequiredString(const json& j, const char* key, const std::filesystem::path& source) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_string() || it->get<std::string>().empty()) {
        throw std::runtime_error("模型配置缺少字段 " + std::string(key) + ": " + PathToUtf8(source));
    }
    return it->get<std::string>();
}

std::string OptionalString(const json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

bool IsInsideOrSame(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    const auto rel = std::filesystem::relative(candidate, root);
    if (rel.empty()) return true;
    for (const auto& part : rel) {
        if (part == "..") return false;
    }
    return true;
}

std::filesystem::path ResolveContained(const std::filesystem::path& root, const std::string& value, const char* label) {
    if (value.empty()) return {};
    std::filesystem::path p = PathFromUtf8(value);
    if (p.is_absolute()) {
        throw std::runtime_error("模型资产路径必须是相对路径 " + std::string(label) + ": " + value);
    }
    const auto root_canonical = std::filesystem::weakly_canonical(root);
    const auto resolved = std::filesystem::weakly_canonical(root / p);
    if (!IsInsideOrSame(root_canonical, resolved)) {
        throw std::runtime_error("模型资产路径越出 voice 根目录 " + std::string(label) + ": " + value);
    }
    return resolved;
}

std::string PathString(const std::filesystem::path& path) {
    if (path.empty()) return {};
    return PathToUtf8(path);
}

std::string ResolveList(const std::filesystem::path& root, const std::string& values, const char* label) {
    std::stringstream input(values);
    std::string item;
    std::string output;
    while (std::getline(input, item, ',')) {
        if (item.empty()) continue;
        if (!output.empty()) output.push_back(',');
        output += PathString(ResolveContained(root, item, label));
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
        RequireFile(PathFromUtf8(item), label);
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
    std::error_code ec;
    if (!std::filesystem::exists(models_root_, ec)) {
        return result;
    }
    std::filesystem::directory_iterator it(models_root_, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec) {
        result.invalid.push_back({models_root_, PathToUtf8(models_root_.filename()), "无法扫描模型目录: " + ec.message()});
        return result;
    }
    for (const auto& dir : it) {
        if (dir.is_directory(ec) && !ec) {
            // handled below
        } else {
            ec.clear();
            continue;
        }
        const auto model_json = dir.path() / "model.json";
        if (!std::filesystem::exists(model_json, ec)) {
            missing_json_dirs.push_back({dir.path(), PathToUtf8(dir.path().filename()), "缺少 model.json"});
            ec.clear();
            continue;
        }
        model_files.push_back(model_json);
    }
    std::sort(model_files.begin(), model_files.end(), [](const auto& left, const auto& right) {
        return PathToUtf8(left) < PathToUtf8(right);
    });
    for (const auto& model_json : model_files) {
        try {
            result.entries.push_back(LoadModelJson(model_json));
        } catch (const std::exception& ex) {
            result.invalid.push_back({model_json.parent_path(), PathToUtf8(model_json.parent_path().filename()), ex.what()});
        }
    }
    std::sort(result.entries.begin(), result.entries.end(), [](const auto& left, const auto& right) {
        return left.id < right.id;
    });
    std::unordered_map<std::string, int> id_counts;
    for (const auto& entry : result.entries) {
        ++id_counts[entry.id];
    }
    if (std::any_of(id_counts.begin(), id_counts.end(), [](const auto& item) { return item.second > 1; })) {
        std::vector<TtsModelEntry> unique_entries;
        for (const auto& entry : result.entries) {
            if (id_counts[entry.id] > 1) {
                result.invalid.push_back({entry.root, entry.id, "重复 model id: " + entry.id});
            } else {
                unique_entries.push_back(entry);
            }
        }
        result.entries = std::move(unique_entries);
    }
    std::sort(missing_json_dirs.begin(), missing_json_dirs.end(), [](const auto& left, const auto& right) {
        return PathToUtf8(left.path) < PathToUtf8(right.path);
    });
    result.invalid.insert(result.invalid.end(), missing_json_dirs.begin(), missing_json_dirs.end());
    return result;
}

TtsModelEntry ModelRegistry::LoadModelJson(const std::filesystem::path& model_json) {
    std::ifstream input(model_json, std::ios::binary);
    if (!input) {
        throw std::runtime_error("无法读取模型配置: " + PathToUtf8(model_json));
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
    if (c.engine_id != "sherpa-vits") {
        throw std::runtime_error("ENGINE_MISMATCH: sherpa registry 不接受 engine_id=" + c.engine_id);
    }
    c.model_path = PathString(ResolveContained(root, RequiredString(j, "model", model_json), "model"));
    c.tokens_path = PathString(ResolveContained(root, RequiredString(j, "tokens", model_json), "tokens"));
    c.data_dir = PathString(ResolveContained(root, OptionalString(j, "data_dir"), "data_dir"));
    c.lexicon_path = PathString(ResolveContained(root, OptionalString(j, "lexicon"), "lexicon"));
    c.rule_fsts = ResolveList(root, OptionalString(j, "rule_fsts"), "rule_fsts");
    c.language_code = j.value("language_code", std::string{});
    c.speaker_id = j.value("speaker_id", 0);
    c.num_threads = j.value("num_threads", 2);
    if (c.language_code.empty()) throw std::runtime_error("模型配置缺少 language_code: " + PathToUtf8(model_json));
    if (c.speaker_id < 0) throw std::runtime_error("模型 speaker_id 不能为负数: " + entry.id);
    if (c.num_threads < 1) throw std::runtime_error("模型 num_threads 必须 >= 1: " + entry.id);

    RequireFile(PathFromUtf8(c.model_path), "model");
    RequireFile(PathFromUtf8(c.tokens_path), "tokens");
    if (!c.data_dir.empty() && !std::filesystem::exists(PathFromUtf8(c.data_dir))) {
        throw std::runtime_error("模型缺失 data_dir: " + c.data_dir);
    }
    RequireFile(PathFromUtf8(c.lexicon_path), "lexicon");
    RequireFileList(c.rule_fsts, "rule_fsts");
    return entry;
}

} // namespace adayo
