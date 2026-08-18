#pragma once

#include "adapters/excel/IWorkbookReader.h"
#include "core/workbook/ColumnAnalyzer.h"
#include "persistence/ConfigModel.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace adayo {

struct WorkbookAnalysis {
    std::filesystem::path path;
    std::string identity;
    std::string sheet_name;
    WorksheetData worksheet;
    std::vector<ColumnProfile> columns;
    std::optional<SheetMappingConfig> saved_mapping;
};

class WorkbookService {
public:
    explicit WorkbookService(std::unique_ptr<IWorkbookReader> reader);

    std::vector<std::string> SheetNames(const std::filesystem::path& path) const;
    WorkbookAnalysis AnalyzeSheet(
        const std::filesystem::path& path,
        const std::string& sheet_name,
        std::size_t header_row,
        const AppConfig& config = {}) const;

    static std::string WorkbookIdentity(const std::filesystem::path& path);
    static std::optional<SheetMappingConfig> FindMapping(
        const AppConfig& config,
        const std::string& workbook_identity,
        const std::string& sheet_name,
        std::size_t header_row);
    static void UpsertMapping(AppConfig& config, SheetMappingConfig mapping);

private:
    std::unique_ptr<IWorkbookReader> reader_;
    ColumnAnalyzer analyzer_;
};

} // namespace adayo
