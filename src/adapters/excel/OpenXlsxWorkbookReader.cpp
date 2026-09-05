#include "adapters/excel/OpenXlsxWorkbookReader.h"

#include "platform/UnicodePath.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

#ifdef ADAYO_HAS_OPENXLSX
#include <OpenXLSX.hpp>
#endif

namespace adayo {

#ifdef ADAYO_HAS_OPENXLSX
namespace {
class ReadOnlyDocument {
public:
    explicit ReadOnlyDocument(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) {
            throw std::runtime_error("工作簿不存在: " + PathToUtf8(path));
        }
        doc_.open(PathToUtf8(path));
    }

    ~ReadOnlyDocument() {
        try {
            doc_.close();
        } catch (...) {
        }
    }

    OpenXLSX::XLDocument& get() { return doc_; }

private:
    OpenXLSX::XLDocument doc_;
};

std::string CellToString(OpenXLSX::XLCellAssignable cell) {
    if (cell.empty()) return {};

    const auto& value = cell.value();
    switch (value.type()) {
        case OpenXLSX::XLValueType::Empty:
            return {};
        case OpenXLSX::XLValueType::Boolean:
            return value.get<bool>() ? "true" : "false";
        case OpenXLSX::XLValueType::Integer:
            return std::to_string(value.get<int64_t>());
        case OpenXLSX::XLValueType::Float: {
            std::ostringstream out;
            out << std::setprecision(15) << value.get<double>();
            return out.str();
        }
        case OpenXLSX::XLValueType::String:
            return value.get<std::string>();
        case OpenXLSX::XLValueType::Error:
        default:
            return value.getString();
    }
}

} // namespace
#endif

std::vector<std::string> OpenXlsxWorkbookReader::SheetNames(const std::filesystem::path& path) {
#ifdef ADAYO_HAS_OPENXLSX
    ReadOnlyDocument doc(path);
    return doc.get().workbook().worksheetNames();
#else
    (void)path;
    throw std::runtime_error("当前构建未启用 OpenXLSX");
#endif
}

WorksheetData OpenXlsxWorkbookReader::ReadSheet(const std::filesystem::path& path, const std::string& sheet, std::size_t header_row) {
#ifdef ADAYO_HAS_OPENXLSX
    if (header_row == 0) {
        throw std::invalid_argument("Excel 表头行号必须从 1 开始");
    }

    ReadOnlyDocument doc(path);
    auto workbook = doc.get().workbook();
    if (!workbook.worksheetExists(sheet)) {
        throw std::runtime_error("工作表不存在: " + sheet);
    }

    auto worksheet = workbook.worksheet(sheet);
    const auto row_count = worksheet.rowCount();
    const auto column_count = worksheet.columnCount();
    if (row_count == 0 || column_count == 0 || header_row > row_count) {
        return WorksheetData{{}, {}, header_row};
    }

    WorksheetData data;
    data.header_row = header_row;
    data.headers.reserve(column_count);
    for (uint16_t column = 1; column <= column_count; ++column) {
        data.headers.push_back(CellToString(worksheet.findCell(static_cast<uint32_t>(header_row), column)));
    }

    std::vector<std::vector<std::string>> rows;
    std::size_t last_meaningful_column = 0;
    for (std::size_t i = 0; i < data.headers.size(); ++i) {
        if (!data.headers[i].empty()) last_meaningful_column = i + 1;
    }
    for (uint32_t row_number = static_cast<uint32_t>(header_row + 1); row_number <= row_count; ++row_number) {
        std::vector<std::string> row;
        row.reserve(column_count);
        for (uint16_t column = 1; column <= column_count; ++column) {
            row.push_back(CellToString(worksheet.findCell(row_number, column)));
            if (!row.back().empty()) {
                last_meaningful_column = (std::max)(last_meaningful_column, static_cast<std::size_t>(column));
            }
        }
        const bool empty = std::all_of(row.begin(), row.end(), [](const std::string& value) { return value.empty(); });
        if (!empty) {
            rows.push_back(std::move(row));
        }
    }
    data.headers.resize(last_meaningful_column);
    for (auto& row : rows) {
        row.resize(last_meaningful_column);
        data.rows.push_back(std::move(row));
    }
    return data;
#else
    (void)path; (void)sheet; (void)header_row;
    throw std::runtime_error("当前构建未启用 OpenXLSX");
#endif
}
} // namespace adayo
