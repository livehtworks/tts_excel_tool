#include "adapters/excel/OpenXlsxWorkbookReader.h"

#include "platform/UnicodePath.h"
#include "platform/FileIo.h"
#include <chrono>
#include <map>

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

std::vector<WorksheetInfo> OpenXlsxWorkbookReader::SheetMetadata(const std::filesystem::path& path) {
#ifdef ADAYO_HAS_OPENXLSX
    ReadOnlyDocument doc(path); std::vector<WorksheetInfo> result;
    for(const auto& name:doc.get().workbook().worksheetNames()) {
        const auto state=doc.get().workbook().worksheet(name).visibility();
        result.push_back({name,state==OpenXLSX::XLSheetState::Visible ? SheetVisibility::Visible :
            (state==OpenXLSX::XLSheetState::Hidden ? SheetVisibility::Hidden : SheetVisibility::VeryHidden)});
    }
    return result;
#else
    (void)path; throw std::runtime_error("OpenXLSX unavailable");
#endif
}

WorksheetData OpenXlsxWorkbookReader::ReadSheet(const std::filesystem::path& path, const std::string& sheet, std::size_t header_row, std::stop_token token) {
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
    WorksheetData data;
    data.header_row = header_row;
    data.source={PathToUtf8(std::filesystem::absolute(path)),{},FileSha256(path),sheet,
        std::to_string(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())),header_row};
    auto& merges=worksheet.merges();
    if(merges.count()>1000000) throw std::runtime_error("Workbook merge count exceeds resource budget");
    for(std::size_t i=0;i<merges.count();++i) {
        const std::string ref=merges.merge(static_cast<OpenXLSX::XLMergeIndex>(i));
        const auto colon=ref.find(':');
        OpenXLSX::XLCellReference first(ref.substr(0,colon)), last(colon==std::string::npos ? ref : ref.substr(colon+1));
        data.merged_ranges.push_back({first.row(),last.row(),first.column(),last.column(),ref});
    }
    std::map<std::size_t,std::map<std::size_t,std::string>> sparse;
    std::size_t max_column=0, visited=0, text_bytes=0, value_count=0;
    auto range=worksheet.rows();
    for(auto it=range.begin();it!=range.end();++it) {
        if(token.stop_requested()) throw std::runtime_error("Workbook import canceled");
        if(!it.rowExists() || it.rowNumber()<header_row) continue;
        auto row=*it;
        const auto count=static_cast<std::size_t>(row.cellCount());
        if(count>16384 || visited>16000000-count) throw std::runtime_error("Workbook cell traversal exceeds resource budget");
        visited+=count;
        for(std::size_t c=1;c<=count;++c) {
            if((c%256)==0 && token.stop_requested()) throw std::runtime_error("Workbook import canceled");
            auto value=CellToString(worksheet.findCell(row.rowNumber(),static_cast<std::uint16_t>(c)));
            if(value.empty()) continue;
            if(value.find('\0')!=std::string::npos) throw std::runtime_error("Embedded NUL in workbook cell");
            if(value.size()>64*1024*1024-text_bytes) throw std::runtime_error("Workbook text exceeds 64MiB budget");
            if(++value_count>(512ull*1024*1024-2*text_bytes)/192) throw std::runtime_error("Workbook sparse values exceed memory budget");
            text_bytes+=value.size(); max_column=(std::max)(max_column,c);
            sparse[row.rowNumber()][c]=std::move(value);
        }
    }
    constexpr std::size_t budget=512ull*1024*1024;
    if(max_column && sparse.size()>(budget-2*text_bytes)/(sizeof(std::string)*max_column+sizeof(std::vector<std::string>)))
        throw std::runtime_error("Workbook rectangular output exceeds 512MiB budget");
    data.headers.resize(max_column);
    for(auto& [row_number,values]:sparse) {
        std::vector<std::string> row(max_column);
        for(auto& [column,value]:values) row[column-1]=std::move(value);
        if(row_number==header_row) data.headers=std::move(row);
        else { data.source_excel_row_numbers.push_back(row_number); data.rows.push_back(std::move(row)); }
    }
    return data;
#else
    (void)path; (void)sheet; (void)header_row; (void)token;
    throw std::runtime_error("当前构建未启用 OpenXLSX");
#endif
}
} // namespace adayo
