#pragma once

#include "adapters/excel/IWorkbookReader.h"

namespace adayo {
class OpenXlsxWorkbookReader final : public IWorkbookReader {
public:
    std::vector<std::string> SheetNames(const std::filesystem::path& path) override;
    WorksheetData ReadSheet(const std::filesystem::path& path, const std::string& sheet, std::size_t header_row) override;
};
} // namespace adayo
