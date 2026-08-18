#include "services/WorkbookService.h"

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

WorkbookAnalysis WorkbookService::AnalyzeSheet(
    const std::filesystem::path& path,
    const std::string& sheet_name,
    std::size_t header_row,
    const AppConfig& config) const {

    WorkbookAnalysis analysis;
    analysis.path = path;
    analysis.identity = WorkbookIdentity(path);
    analysis.sheet_name = sheet_name;
    analysis.worksheet = reader_->ReadSheet(path, sheet_name, header_row);
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
                column.language_code = saved->language_code;
                column.tts_engine_id = saved->tts_engine_id;
            }
        }
    }

    return analysis;
}

std::string WorkbookService::WorkbookIdentity(const std::filesystem::path& path) {
    std::error_code ec;
    const auto absolute = std::filesystem::weakly_canonical(path, ec);
    const auto effective = ec ? std::filesystem::absolute(path, ec) : absolute;
    const auto last_write = std::filesystem::exists(path) ? std::filesystem::last_write_time(path, ec).time_since_epoch().count() : 0;
    const auto size = std::filesystem::exists(path) ? std::filesystem::file_size(path, ec) : 0;
    const auto u8 = effective.u8string();
    std::string path_text(u8.begin(), u8.end());
    std::ostringstream out;
    out << path_text << "|" << size << "|" << last_write;
    return out.str();
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
