#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>
#include <stop_token>
#include "core/domain/Types.h"

namespace adayo {
struct WorksheetData {
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
    std::size_t header_row{1};
    std::vector<std::size_t> source_excel_row_numbers;
    std::vector<MergedRange> merged_ranges;
    WorkbookSource source;
};
enum class SheetVisibility { Visible, Hidden, VeryHidden };
struct WorksheetInfo { std::string name; SheetVisibility visibility{SheetVisibility::Visible}; };

class IWorkbookReader {
public:
    virtual ~IWorkbookReader() = default;
    virtual std::vector<std::string> SheetNames(const std::filesystem::path& path) = 0;
    virtual std::vector<WorksheetInfo> SheetMetadata(const std::filesystem::path& path) = 0;
    virtual WorksheetData ReadSheet(const std::filesystem::path& path, const std::string& sheet, std::size_t header_row, std::stop_token token = {}) = 0;
};
} // namespace adayo
