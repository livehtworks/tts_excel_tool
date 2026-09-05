#include "services/WorkbookService.h"

#include "platform/UnicodePath.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace adayo {

WorkbookService::WorkbookService(std::unique_ptr<IWorkbookReader> reader)
    : reader_(std::move(reader)) {
    if (!reader_) {
        throw std::invalid_argument("Workbook reader 不能为空");
    }
}

std::vector<std::string> WorkbookService::SheetNames(const std::filesystem::path& path) const {
    return reader_->SheetNames(path);
}
std::vector<WorksheetInfo> WorkbookService::SheetMetadata(const std::filesystem::path& path) const { return reader_->SheetMetadata(path); }

WorkbookAnalysis WorkbookService::AnalyzeSheet(
    const std::filesystem::path& path,
    const std::string& sheet_name,
    std::size_t header_row,
    const AppConfig& config, std::stop_token token) const {

    WorkbookAnalysis analysis;
    analysis.path = path;
    analysis.identity = WorkbookIdentity(path);
    analysis.sheet_name = sheet_name;
    analysis.worksheet = reader_->ReadSheet(path, sheet_name, header_row, token);
    analysis.worksheet.source.identity=analysis.identity;
    analysis.columns = analyzer_.Analyze(analysis.worksheet.headers, analysis.worksheet.rows);
    analysis.saved_mapping = FindMapping(config, analysis.identity, sheet_name, header_row);

    if (analysis.saved_mapping) {
        for (auto& column : analysis.columns) {
            auto saved = std::find_if(
                analysis.saved_mapping->columns.begin(),
                analysis.saved_mapping->columns.end(),
                [&](const ColumnProfile& item) {
                    return item.source_index == column.source_index && item.header == column.header;
                });
            if (saved != analysis.saved_mapping->columns.end()) {
                column.selected = saved->selected;
                column.role = saved->role;
                if (saved->language_selection_mode == LanguageSelectionMode::Fixed || saved->language_user_overridden) {
                    column.language_code = saved->language_code;
                    column.language_user_overridden = true;
                    column.language_selection_mode = LanguageSelectionMode::Fixed;
                } else {
                    column.language_selection_mode = LanguageSelectionMode::Auto;
                }
                column.tts_engine_id = saved->tts_engine_id;
                column.tts_model_id = saved->tts_model_id;
            }
        }
    }

    return analysis;
}

std::string WorkbookService::WorkbookIdentity(const std::filesystem::path& path) {
    std::error_code ec;
    const auto absolute = std::filesystem::weakly_canonical(path, ec);
    const auto effective = ec ? std::filesystem::absolute(path, ec) : absolute;
    std::string path_text = PathToUtf8(effective);
    std::replace(path_text.begin(), path_text.end(), '\\', '/');
#ifdef _WIN32
    std::transform(path_text.begin(), path_text.end(), path_text.begin(), [](unsigned char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
    });
#endif
    return path_text;
}

std::optional<SheetMappingConfig> WorkbookService::FindMapping(
    const AppConfig& config,
    const std::string& workbook_identity,
    const std::string& sheet_name,
    std::size_t header_row) {

    for (const auto& mapping : config.sheet_mappings) {
        if (mapping.workbook_identity == workbook_identity &&
            mapping.sheet_name == sheet_name &&
            mapping.header_row == header_row) {
            return mapping;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> WorkbookService::FindHeaderRow(
    const AppConfig& config,
    const std::string& workbook_identity,
    const std::string& sheet_name) {

    for (const auto& row : config.sheet_header_rows) {
        if (row.workbook_identity == workbook_identity && row.sheet_name == sheet_name) {
            return row.header_row;
        }
    }
    return std::nullopt;
}

void WorkbookService::UpsertHeaderRow(
    AppConfig& config,
    std::string workbook_identity,
    std::string sheet_name,
    std::size_t header_row) {

    auto existing = std::find_if(
        config.sheet_header_rows.begin(),
        config.sheet_header_rows.end(),
        [&](const SheetHeaderRowConfig& item) {
            return item.workbook_identity == workbook_identity && item.sheet_name == sheet_name;
        });
    if (existing == config.sheet_header_rows.end()) {
        config.sheet_header_rows.push_back({std::move(workbook_identity), std::move(sheet_name), header_row});
    } else {
        existing->header_row = header_row;
    }
}

void WorkbookService::UpsertMapping(AppConfig& config, SheetMappingConfig mapping) {
    auto existing = std::find_if(
        config.sheet_mappings.begin(),
        config.sheet_mappings.end(),
        [&](const SheetMappingConfig& item) {
            return item.workbook_identity == mapping.workbook_identity &&
                   item.sheet_name == mapping.sheet_name &&
                   item.header_row == mapping.header_row;
        });
    if (existing == config.sheet_mappings.end()) {
        config.sheet_mappings.push_back(std::move(mapping));
    } else {
        *existing = std::move(mapping);
    }
}

} // namespace adayo
