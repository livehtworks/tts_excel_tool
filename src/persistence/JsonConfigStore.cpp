#include "persistence/JsonConfigStore.h"

#include "platform/UnicodePath.h"
#include "platform/FileIo.h"
#include <mutex>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace adayo {
using nlohmann::json;

namespace {
constexpr int kCurrentSchemaVersion = 4;
class ConfigValidationError final : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
void ValidateCacheConfig(const AudioCacheOptions& value) {
    if (value.memory_limit_bytes!=64ull*1024*1024 || value.disk_limit_bytes<128ull*1024*1024 ||
        value.disk_limit_bytes>16384ull*1024*1024 || value.entry_limit!=20000)
        throw ConfigValidationError("Invalid audio_cache limits (memory 64MiB, disk 128-16384MiB, entries 20000)");
}

class FutureSchemaError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

std::string Timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y%m%d-%H%M%S");
    return out.str();
}

std::filesystem::path CorruptBackupPath(const std::filesystem::path& path) {
    auto candidate = path;
    candidate += ".corrupt-" + Timestamp();
    int suffix = 1;
    while (std::filesystem::exists(candidate)) {
        candidate = path;
        candidate += ".corrupt-" + Timestamp() + "-" + std::to_string(suffix++);
    }
    return candidate;
}

void AtomicReplace(const std::filesystem::path& temp, const std::filesystem::path& final) {
#ifdef _WIN32
    if (!MoveFileExW(temp.wstring().c_str(), final.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::runtime_error("原子替换配置失败: " + std::to_string(GetLastError()));
    }
#else
    std::filesystem::rename(temp, final);
#endif
}

void RejectUnsafeExistingTarget(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("无法读取现有配置文件，拒绝覆盖: " + PathToUtf8(path));
    }
    json document;
    try {
        input >> document;
    } catch (const std::exception& ex) {
        throw std::runtime_error("现有配置文件损坏，拒绝覆盖，请先备份或让程序完成 corrupt 备份: " + std::string(ex.what()));
    }
    const int schema_version = document.value("schema_version", 1);
    if (schema_version > kCurrentSchemaVersion) {
        throw FutureSchemaError("配置 schema_version 来自未来版本，拒绝覆盖");
    }
}

std::string ToString(ColumnRole role) {
    switch (role) {
        case ColumnRole::Ignore: return "ignore";
        case ColumnRole::Reference: return "reference";
        case ColumnRole::Play: return "play";
        case ColumnRole::Result: return "result";
        case ColumnRole::Index: return "index";
    }
    return "ignore";
}

ColumnRole ColumnRoleFromString(const std::string& value) {
    if (value == "reference") return ColumnRole::Reference;
    if (value == "play") return ColumnRole::Play;
    if (value == "result") return ColumnRole::Result;
    if (value == "index") return ColumnRole::Index;
    return ColumnRole::Ignore;
}

std::string ToString(SuggestedColumnType type) {
    switch (type) {
        case SuggestedColumnType::Unknown: return "unknown";
        case SuggestedColumnType::Meta: return "meta";
        case SuggestedColumnType::Utterance: return "utterance";
        case SuggestedColumnType::Result: return "result";
    }
    return "unknown";
}

SuggestedColumnType SuggestedColumnTypeFromString(const std::string& value) {
    if (value == "meta") return SuggestedColumnType::Meta;
    if (value == "utterance") return SuggestedColumnType::Utterance;
    if (value == "result") return SuggestedColumnType::Result;
    return SuggestedColumnType::Unknown;
}

std::string ToString(LanguageSelectionMode mode) {
    switch (mode) {
        case LanguageSelectionMode::Auto: return "auto";
        case LanguageSelectionMode::Fixed: return "fixed";
    }
    return "auto";
}

LanguageSelectionMode LanguageSelectionModeFromString(const std::string& value) {
    if (value == "fixed") return LanguageSelectionMode::Fixed;
    return LanguageSelectionMode::Auto;
}
} // namespace

void to_json(json& j, const ColumnProfile& column) {
    j = json{
        {"source_index", column.source_index},
        {"excel_column", column.excel_column},
        {"header", column.header},
        {"non_empty_count", column.non_empty_count},
        {"samples", column.samples},
        {"suggested_type", ToString(column.suggested_type)},
        {"guessed_language", column.guessed_language},
        {"selected", column.selected},
        {"role", ToString(column.role)},
        {"language_code", column.language_code},
        {"language_user_overridden", column.language_user_overridden},
        {"language_selection_mode", ToString(column.language_selection_mode)},
        {"tts_engine_id", column.tts_engine_id},
        {"tts_model_id", column.tts_model_id},
    };
}

void from_json(const json& j, ColumnProfile& column) {
    column.source_index = j.value("source_index", std::size_t{});
    column.excel_column = j.value("excel_column", std::string{});
    column.header = j.value("header", std::string{});
    column.non_empty_count = j.value("non_empty_count", std::size_t{});
    column.samples = j.value("samples", std::vector<std::string>{});
    column.suggested_type = SuggestedColumnTypeFromString(j.value("suggested_type", std::string{}));
    column.guessed_language = j.value("guessed_language", std::string{});
    column.selected = j.value("selected", false);
    column.role = ColumnRoleFromString(j.value("role", std::string{}));
    column.language_code = j.value("language_code", std::string{});
    column.language_user_overridden = j.value("language_user_overridden", false);
    column.language_selection_mode = LanguageSelectionModeFromString(j.value("language_selection_mode", std::string{}));
    if (column.language_user_overridden && !j.contains("language_selection_mode")) {
        column.language_selection_mode = LanguageSelectionMode::Fixed;
    }
    column.language_user_overridden = column.language_selection_mode == LanguageSelectionMode::Fixed;
    column.tts_engine_id = j.value("tts_engine_id", std::string{"sherpa-vits"});
    column.tts_model_id = j.value("tts_model_id", std::string{});
}

void to_json(json& j, const SheetHeaderRowConfig& row) {
    j = json{
        {"workbook_identity", row.workbook_identity},
        {"sheet_name", row.sheet_name},
        {"header_row", row.header_row},
    };
}

void from_json(const json& j, SheetHeaderRowConfig& row) {
    row.workbook_identity = j.value("workbook_identity", std::string{});
    row.sheet_name = j.value("sheet_name", std::string{});
    row.header_row = j.value("header_row", std::size_t{1});
}

void to_json(json& j, const SheetMappingConfig& mapping) {
    j = json{
        {"workbook_identity", mapping.workbook_identity},
        {"sheet_name", mapping.sheet_name},
        {"header_row", mapping.header_row},
        {"columns", mapping.columns},
    };
}

void from_json(const json& j, SheetMappingConfig& mapping) {
    mapping.workbook_identity = j.value("workbook_identity", std::string{});
    mapping.sheet_name = j.value("sheet_name", std::string{});
    mapping.header_row = j.value("header_row", std::size_t{1});
    mapping.columns = j.value("columns", std::vector<ColumnProfile>{});
}

void to_json(json& j, const AppConfig& config) {
    ValidateCacheConfig(config.audio_cache);
    j = json{
        {"schema_version", kCurrentSchemaVersion},
        {"audio_cache", {{"enabled",config.audio_cache.enabled},{"memory_limit_bytes",config.audio_cache.memory_limit_bytes},
            {"disk_limit_bytes",config.audio_cache.disk_limit_bytes},{"entry_limit",config.audio_cache.entry_limit}}},
        {"last_workbook", config.last_workbook},
        {"last_sheet", config.last_sheet},
        {"speech_rate", config.speech_rate},
        {"alignment_threshold", config.alignment_threshold},
        {"pass_threshold", config.pass_threshold},
        {"sheet_header_rows", config.sheet_header_rows},
        {"sheet_mappings", config.sheet_mappings},
    };
}

void from_json(const json& j, AppConfig& config) {
    const int schema_version = j.value("schema_version", 1);
    if (schema_version > kCurrentSchemaVersion) {
        throw FutureSchemaError("配置 schema_version 来自未来版本，拒绝读取");
    }
    config.schema_version = kCurrentSchemaVersion;
    if (j.contains("audio_cache")) {
        try {
            const auto& value=j.at("audio_cache");
            config.audio_cache.enabled=value.value("enabled",true);
            config.audio_cache.memory_limit_bytes=value.value("memory_limit_bytes",64ull*1024*1024);
            config.audio_cache.disk_limit_bytes=value.value("disk_limit_bytes",2048ull*1024*1024);
            config.audio_cache.entry_limit=value.value("entry_limit",std::size_t{20000});
            ValidateCacheConfig(config.audio_cache);
        } catch (const std::exception& ex) { throw ConfigValidationError(ex.what()); }
    }
    config.last_workbook = j.value("last_workbook", std::string{});
    config.last_sheet = j.value("last_sheet", std::string{});
    config.speech_rate = j.value("speech_rate", 1.0);
    config.alignment_threshold = j.value("alignment_threshold", 80.0);
    config.pass_threshold = j.value("pass_threshold", 100.0);
    config.sheet_header_rows = j.value("sheet_header_rows", std::vector<SheetHeaderRowConfig>{});
    config.sheet_mappings = j.value("sheet_mappings", std::vector<SheetMappingConfig>{});
    if (schema_version < 2) {
        for (auto& mapping : config.sheet_mappings) {
            for (auto& column : mapping.columns) {
                column.language_user_overridden = false;
                column.language_selection_mode = LanguageSelectionMode::Auto;
            }
        }
    } else if (schema_version < 3) {
        for (auto& mapping : config.sheet_mappings) {
            for (auto& column : mapping.columns) {
                column.language_selection_mode = column.language_user_overridden
                    ? LanguageSelectionMode::Fixed
                    : LanguageSelectionMode::Auto;
                column.language_user_overridden = column.language_selection_mode == LanguageSelectionMode::Fixed;
            }
        }
    }
}

JsonConfigStore::JsonConfigStore(std::filesystem::path path) : path_(std::move(path)) {}

AppConfig JsonConfigStore::Load() const {
    if (!std::filesystem::exists(path_)) {
        return {};
    }

    std::ifstream input(path_, std::ios::binary);
    if (!input) {
        throw std::runtime_error("无法读取配置文件");
    }

    json document;
    input >> document;
    return document.get<AppConfig>();
}

ConfigLoadResult JsonConfigStore::LoadOrDefault() const {
    ConfigLoadResult result;
    if (!std::filesystem::exists(path_)) {
        result.status = ConfigLoadStatus::Missing;
        return result;
    }
    try {
        result.config = Load();
        result.status = ConfigLoadStatus::Loaded;
        return result;
    } catch (const FutureSchemaError& ex) {
        result.status = ConfigLoadStatus::FutureSchema;
        result.allow_save = false;
        result.message = ex.what();
        return result;
    } catch (const ConfigValidationError& ex) {
        result.status = ConfigLoadStatus::InvalidValues;
        result.allow_save = false;
        result.message = ex.what();
        return result;
    } catch (const std::exception& ex) {
        const auto backup = CorruptBackupPath(path_);
        std::error_code ec;
        std::filesystem::rename(path_, backup, ec);
        if (ec) {
            result.status = ConfigLoadStatus::CorruptBackupFailed;
            result.allow_save = false;
            result.message = "配置文件损坏且备份失败，未覆盖原文件: " + ec.message() + "；原始错误: " + ex.what();
            return result;
        }
        result.status = ConfigLoadStatus::CorruptBackedUp;
        result.backup_path = backup;
        result.message = "配置文件损坏，已备份为 " + PathToUtf8(backup) + "；本次使用默认配置: " + ex.what();
        return result;
    }
}

void JsonConfigStore::Save(const AppConfig& config) const {
    static std::mutex save_mutex;
    std::lock_guard lock(save_mutex);
    if (path_.has_parent_path()) {
        std::filesystem::create_directories(path_.parent_path());
    }
    RejectUnsafeExistingTarget(path_);

    json document = config;
    const auto serialized=document.dump(2);
    WriteBinaryFileAtomically(path_,serialized.data(),serialized.size());
}

} // namespace adayo
