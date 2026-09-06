#include "services/ModelRegistry.h"

#include "platform/UnicodePath.h"
#include "platform/FileIo.h"

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
    if (!std::filesystem::is_regular_file(path)) {
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

std::string VoiceValidationObservation::Status(const std::string& current_model_sha256) const {
    if(!current_model_sha256.empty() && current_model_sha256!=model_sha256) return "历史观察已过期：权重已变化";
    const auto identity=current_model_sha256.empty() ? "；当前身份未核对" : "；非完整当前前端证明";
    return std::string(warnings.empty() ? "历史出声检查通过" : "曾出现音素告警")+identity;
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
            auto entry = LoadModelJson(model_json);
            result.entries.push_back(entry);
            for (const auto& [id, name] : entry.speakers) {
                if (id == entry.config.speaker_id) continue;
                TtsModelEntry speaker{entry.id + "::speaker-" + std::to_string(id),
                    entry.display_name + " / " + name, entry.root, entry.config, {}};
                speaker.config.speaker_id = id;
                result.entries.push_back(std::move(speaker));
            }
        } catch (const std::exception& ex) {
            result.invalid.push_back({model_json.parent_path(), PathToUtf8(model_json.parent_path().filename()), ex.what()});
        }
    }
    const auto observations=models_root_.parent_path().parent_path()/"voice-validation-observations.json";
    if(std::filesystem::exists(observations)) {
        try {
            RequireOrdinaryPath(observations);
            if(std::filesystem::file_size(observations)>16*1024*1024) throw std::runtime_error("Observation file exceeds 16 MiB");
            std::ifstream input(observations,std::ios::binary);
            const auto document=json::parse(input);
            if(document.at("schema_version")!=1) throw std::runtime_error("Unsupported observation schema");
            std::unordered_map<std::string,VoiceValidationObservation> by_id;
            for(const auto& voice:document.at("voices")) {
                VoiceValidationObservation observation;
                observation.observed_at=voice.at("observed_at").get<std::string>();
                observation.run_id=voice.at("run_id").get<std::string>();
                observation.model_sha256=voice.at("model_sha256").get<std::string>();
                observation.frontend_rule_coverage=voice.at("frontend_rule_coverage").get<std::string>();
                observation.evidence_scope=voice.at("evidence_scope").get<std::string>();
                observation.warnings=voice.at("warnings").get<std::vector<std::string>>();
                if(observation.model_sha256.size()!=64 || observation.observed_at.empty() || observation.run_id.empty())
                    throw std::runtime_error("Incomplete observation identity");
                if(!by_id.emplace(voice.at("model_id").get<std::string>(),std::move(observation)).second)
                    throw std::runtime_error("Duplicate observation model ID");
            }
            for(auto& entry:result.entries) {
                auto found=by_id.find(entry.id);
                if(found==by_id.end()) {
                    for(const auto& owner:result.entries) if(owner.root==entry.root && !owner.speakers.empty()) {
                        found=by_id.find(owner.id); break;
                    }
                }
                if(found!=by_id.end()) entry.observation=found->second;
            }
        } catch(const std::exception& ex) {
            for(auto& entry:result.entries) entry.observation_error=ex.what();
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

TtsModelEntry ModelRegistry::LoadModelJson(const std::filesystem::path& model_json, const std::string& offline_transaction) {
    std::ifstream input(model_json, std::ios::binary);
    if (!input) {
        throw std::runtime_error("无法读取模型配置: " + PathToUtf8(model_json));
    }
    json j;
    input >> j;

    const auto root = model_json.parent_path();
    const auto marker=root/".preparation-incomplete.json";
    if(std::filesystem::exists(marker)) {
        if(offline_transaction.empty()) throw std::runtime_error("MODEL_UPDATE_INCOMPLETE: "+PathToUtf8(marker));
        if(model_json.filename()!=PathFromUtf8("model.pending."+offline_transaction+".json"))
            throw std::runtime_error("Offline probe must name its transaction's pending candidate");
        RequireOrdinaryPath(marker);
        std::ifstream marker_input(marker,std::ios::binary);
        const auto pending=json::parse(marker_input);
        if(pending.at("transaction_id").get<std::string>()!=offline_transaction)
            throw std::runtime_error("Offline transaction ID mismatch");
        const auto journal=root.parent_path().parent_path()/".voice-transactions"/offline_transaction/"journal.json";
        RequireOrdinaryPath(journal);
        std::ifstream journal_input(journal,std::ios::binary);
        const auto publication=json::parse(journal_input);
        if(publication.at("transaction_id").get<std::string>()!=offline_transaction || publication.at("stage")!="DEPLOYED_PROBE" ||
            std::filesystem::weakly_canonical(PathFromUtf8(publication.at("target").get<std::string>()))!=std::filesystem::weakly_canonical(root))
            throw std::runtime_error("Offline probe requires the active deployed-probe journal");
        bool candidate_verified=false;
        for(const auto& item:publication.at("files")) {
            const auto path=PathFromUtf8(item.at("path").get<std::string>());
            if(path.filename()=="model.json") continue;
            const auto owned_root=root.parent_path().parent_path();
            const auto relative=std::filesystem::weakly_canonical(path).lexically_relative(std::filesystem::weakly_canonical(owned_root));
            if(relative.empty() || *relative.begin()=="..") throw std::runtime_error("Offline journal path escapes model root");
            RequireOrdinaryPath(path);
            if(FileSha256(path)!=item.at("candidate_sha256").get<std::string>()) throw std::runtime_error("Offline deployed resource hash mismatch");
            if(std::filesystem::equivalent(path,model_json)) candidate_verified=true;
        }
        if(!candidate_verified) throw std::runtime_error("Offline candidate is absent from publication journal");
    } else if(!offline_transaction.empty()) throw std::runtime_error("Offline probe has no active transaction marker");
    TtsModelEntry entry;
    entry.root = root;
    entry.id = RequiredString(j, "id", model_json);
    entry.display_name = j.value("display_name", entry.id);

    auto& c = entry.config;
    c.resource_root=PathToUtf8(std::filesystem::weakly_canonical(root));
    c.offline_transaction=offline_transaction;
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
    c.text_normalization = j.value("text_normalization", std::string{"none"});
    if (c.text_normalization != "none" && c.text_normalization != "nfd")
        throw std::runtime_error("MODEL_INVALID: unsupported text_normalization: " + entry.id);
    if (c.language_code.empty()) throw std::runtime_error("模型配置缺少 language_code: " + PathToUtf8(model_json));
    if (c.speaker_id < 0) throw std::runtime_error("模型 speaker_id 不能为负数: " + entry.id);
    if (c.num_threads < 1) throw std::runtime_error("模型 num_threads 必须 >= 1: " + entry.id);
    if (j.contains("speakers") != j.contains("num_speakers"))
        throw std::runtime_error("MODEL_INVALID: speakers and num_speakers must be supplied together: " + entry.id);
    if (j.contains("speakers")) {
        const auto& speakers = j.at("speakers");
        if (!j.at("num_speakers").is_number_integer() || (j.contains("speaker_id") && !j.at("speaker_id").is_number_integer()))
            throw std::runtime_error("MODEL_INVALID: speaker count/id must be integers: " + entry.id);
        const auto count = j.at("num_speakers").get<int>();
        if (!speakers.is_array() || count < 1 || count > 10000 || speakers.size() != static_cast<std::size_t>(count))
            throw std::runtime_error("MODEL_INVALID: incomplete speakers: " + entry.id);
        std::vector<bool> seen(count, false);
        for (const auto& speaker : speakers) {
            if (!speaker.at("id").is_number_integer())
                throw std::runtime_error("MODEL_INVALID: speaker id must be an integer: " + entry.id);
            const auto id = speaker.at("id").get<int>();
            const auto name = RequiredString(speaker, "name", model_json);
            if (id < 0 || id >= count || seen[id])
                throw std::runtime_error("MODEL_INVALID: duplicate/out-of-range speaker: " + entry.id);
            seen[id] = true;
            entry.speakers.emplace_back(id, name);
        }
        if (c.speaker_id >= count)
            throw std::runtime_error("MODEL_INVALID: default speaker out of range: " + entry.id);
    }

    RequireFile(PathFromUtf8(c.model_path), "model");
    RequireFile(PathFromUtf8(c.tokens_path), "tokens");
    if (!c.data_dir.empty() && !std::filesystem::is_directory(PathFromUtf8(c.data_dir))) {
        throw std::runtime_error("模型缺失 data_dir: " + c.data_dir);
    }
    RequireFile(PathFromUtf8(c.lexicon_path), "lexicon");
    RequireFileList(c.rule_fsts, "rule_fsts");
    return entry;
}

} // namespace adayo
