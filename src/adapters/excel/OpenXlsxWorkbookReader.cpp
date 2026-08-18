#include "adapters/excel/OpenXlsxWorkbookReader.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

#ifdef ADAYO_HAS_OPENXLSX
#include <OpenXLSX.hpp>
#endif

namespace adayo {

#ifdef ADAYO_HAS_OPENXLSX
namespace {
bool ContainsNonAscii(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::any_of(text.begin(), text.end(), [](char8_t c) {
        return static_cast<unsigned char>(c) >= 0x80;
    });
}

std::string NarrowUtf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

std::filesystem::path MakeAsciiStagingPath(const std::filesystem::path& original) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto hash = std::hash<std::string>{}(NarrowUtf8(original));
    auto dir = std::filesystem::temp_directory_path() / "adayo_openxlsx";
    std::filesystem::create_directories(dir);
    return dir / ("workbook_" + std::to_string(hash) + "_" + std::to_string(ticks) + ".xlsx");
}

class ReadOnlyDocument {
public:
    explicit ReadOnlyDocument(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) {
            throw std::runtime_error("工作簿不存在: " + NarrowUtf8(path));
        }
        if (ContainsNonAscii(path)) {
            staged_path_ = MakeAsciiStagingPath(path);
            std::filesystem::copy_file(path, *staged_path_, std::filesystem::copy_options::overwrite_existing);
            doc_.open(staged_path_->string());
        } else {
            doc_.open(path.string());
        }
    }

    ~ReadOnlyDocument() {
        try {
            doc_.close();
        } catch (...) {
        }
        if (staged_path_) {
            std::error_code ec;
            std::filesystem::remove(*staged_path_, ec);
        }
    }

    OpenXLSX::XLDocument& get() { return doc_; }

private:
    OpenXLSX::XLDocument doc_;
    std::optional<std::filesystem::path> staged_path_;
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

void TrimTrailingEmptyCells(std::vector<std::string>& row) {
    while (!row.empty() && row.back().empty()) {
        row.pop_back();
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
    TrimTrailingEmptyCells(data.headers);

    const auto effective_columns = static_cast<uint16_t>(data.headers.empty() ? column_count : data.headers.size());
    for (uint32_t row_number = static_cast<uint32_t>(header_row + 1); row_number <= row_count; ++row_number) {
        std::vector<std::string> row;
        row.reserve(effective_columns);
        for (uint16_t column = 1; column <= effective_columns; ++column) {
            row.push_back(CellToString(worksheet.findCell(row_number, column)));
        }
        TrimTrailingEmptyCells(row);
        const bool empty = std::all_of(row.begin(), row.end(), [](const std::string& value) { return value.empty(); });
        if (!empty) {
            row.resize(effective_columns);
            data.rows.push_back(std::move(row));
        }
    }
    return data;
#else
    (void)path; (void)sheet; (void)header_row;
    throw std::runtime_error("当前构建未启用 OpenXLSX");
#endif
}
} // namespace adayo
