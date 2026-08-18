#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace adayo {
struct WorksheetData {
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
    std::size_t header_row{1};
};

class IWorkbookReader {
public:
    virtual ~IWorkbookReader() = default;
    virtual std::vector<std::string> SheetNames(const std::filesystem::path& path) = 0;
    virtual WorksheetData ReadSheet(const std::filesystem::path& path, const std::string& sheet, std::size_t header_row) = 0;
};
} // namespace adayo
