#include "adapters/excel/OpenXlsxWorkbookReader.h"

#include <stdexcept>

#ifdef ADAYO_HAS_OPENXLSX
#include <OpenXLSX.hpp>
#endif

namespace adayo {
std::vector<std::string> OpenXlsxWorkbookReader::SheetNames(const std::filesystem::path& path) {
#ifdef ADAYO_HAS_OPENXLSX
    OpenXLSX::XLDocument doc;
    doc.open(path.string());
    auto names = doc.workbook().worksheetNames();
    doc.close();
    return names;
#else
    (void)path;
    throw std::runtime_error("当前构建未启用 OpenXLSX");
#endif
}

WorksheetData OpenXlsxWorkbookReader::ReadSheet(const std::filesystem::path& path, const std::string& sheet, std::size_t header_row) {
    // The exact OpenXLSX range iteration is deliberately completed in P2 against the pinned 0.5.x API.
    // Keeping this adapter incomplete prevents accidental dependence on stale 0.4.x behavior.
    (void)path; (void)sheet; (void)header_row;
    throw std::runtime_error("P2: OpenXLSX 0.5.x worksheet reader 尚待完成并用旧版业务样本验收");
}
} // namespace adayo
