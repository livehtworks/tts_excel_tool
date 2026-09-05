#include "ui/CompareGridTable.h"

#include "ui/UiString.h"

#include <algorithm>
#include <utility>

namespace adayo::ui {
namespace {
const char* StatusText(CompareStatus status) {
    switch (status) {
        case CompareStatus::Ok: return "OK";
        case CompareStatus::Ng: return "NG";
        case CompareStatus::Missing: return "MISSING";
        case CompareStatus::Extra: return "EXTRA";
    }
    return "NG";
}

const char* ColumnSuffix(int offset) {
    switch (offset) {
        case 0: return "正式文本";
        case 1: return "机器文本";
        case 2: return "相似度";
        case 3: return "结果";
    }
    return "";
}
} // namespace

CompareGridTable::CompareGridTable(std::shared_ptr<const std::vector<CompareReportGroup>> reports)
    : reports_(std::move(reports)) {
    if (!reports_) {
        reports_ = std::make_shared<const std::vector<CompareReportGroup>>();
    }
}

int CompareGridTable::GetNumberRows() {
    std::size_t rows = 0;
    for (const auto& group : *reports_) {
        rows = (std::max)(rows, group.rows.size());
    }
    return static_cast<int>(rows);
}

int CompareGridTable::GetNumberCols() {
    return static_cast<int>(reports_->size() * 4);
}

wxString CompareGridTable::GetValue(int row, int col) {
    if (row < 0 || col < 0) return {};
    const auto group_index = static_cast<std::size_t>(col / 4);
    const auto offset = col % 4;
    if (group_index >= reports_->size()) return {};
    const auto& group = (*reports_)[group_index];
    if (static_cast<std::size_t>(row) >= group.rows.size()) return {};
    const auto& item = group.rows[static_cast<std::size_t>(row)];
    switch (offset) {
        case 0: return WxUtf8(item.reference_text);
        case 1: return WxUtf8(item.actual_text);
        case 2: return wxString::Format("%.2f", item.similarity);
        case 3: return WxUtf8(StatusText(item.status));
    }
    return {};
}

void CompareGridTable::SetValue(int, int, const wxString&) {}

wxString CompareGridTable::GetColLabelValue(int col) {
    if (col < 0) return {};
    const auto group_index = static_cast<std::size_t>(col / 4);
    if (group_index >= reports_->size()) return {};
    const auto prefix = (*reports_)[group_index].label;
    const auto suffix = ColumnSuffix(col % 4);
    if (prefix.empty()) return WxUtf8(suffix);
    return WxUtf8(prefix + " " + suffix);
}

void CompareGridTable::SetReports(std::shared_ptr<const std::vector<CompareReportGroup>> reports) {
    reports_ = std::move(reports);
    if (!reports_) {
        reports_ = std::make_shared<const std::vector<CompareReportGroup>>();
    }
}

} // namespace adayo::ui
