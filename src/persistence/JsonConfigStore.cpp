#include "persistence/JsonConfigStore.h"

#include <fstream>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

namespace adayo {
using nlohmann::json;

namespace {
constexpr int kCurrentSchemaVersion = 2;


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
    j = json{
        {"schema_version", kCurrentSchemaVersion},
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
        throw std::runtime_error("配置 schema_version 来自未来版本，拒绝读取");
    }
    config.schema_version = kCurrentSchemaVersion;
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

void JsonConfigStore::Save(const AppConfig& config) const {
    if (path_.has_parent_path()) {
        std::filesystem::create_directories(path_.parent_path());
    }

    std::ofstream output(path_, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("无法写入配置文件");
    }

    json document = config;
    output << document.dump(2);
}

} // namespace adayo
