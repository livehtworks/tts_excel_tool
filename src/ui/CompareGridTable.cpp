#include "ui/CompareGridTable.h"

#include "ui/UiString.h"
#include "services/CompareService.h"

#include <algorithm>
#include <utility>
#include <stdexcept>

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
    return current_group_<reports_->size() ? static_cast<int>((*reports_)[current_group_].rows.size()) : 0;
}

int CompareGridTable::GetNumberCols() {
    return reports_->empty() ? 0 : 4;
}

wxString CompareGridTable::GetValue(int row, int col) {
    if (row < 0 || col < 0) return {};
    if(col>=4) return {};
    const auto group_index = current_group_;
    const auto offset = col;
    if (group_index >= reports_->size()) return {};
    const auto& group = (*reports_)[group_index];
    if (static_cast<std::size_t>(row) >= group.rows.size()) return {};
    const auto& item = group.rows[static_cast<std::size_t>(row)];
    switch (offset) {
        case 0: return WxUtf8(item.reference_text);
        case 1: return WxUtf8(item.actual_text);
        case 2:
            if(item.metric==CompareMetric::Cer || item.metric==CompareMetric::Wer)
                return item.error_rate?wxString::Format("%.2f",*item.error_rate*100):
                    WxUtf8("N=0，未定义；插入"+std::to_string(item.edits.insertions));
            return wxString::Format("%.2f", item.similarity);
        case 3: return WxUtf8(StatusText(item.status));
    }
    return {};
}

void CompareGridTable::SetValue(int, int, const wxString&) {}

wxString CompareGridTable::GetColLabelValue(int col) {
    if (col < 0) return {};
    if(col>=4) return {};
    const auto group_index = current_group_;
    if (group_index >= reports_->size()) return {};
    const auto prefix = (*reports_)[group_index].label;
    const auto suffix = col%4==2?CompareService::ValueLabel((*reports_)[group_index].options.metric):std::string(ColumnSuffix(col%4));
    if (prefix.empty()) return WxUtf8(suffix);
    return WxUtf8(prefix + "\n" + suffix);
}

void CompareGridTable::SetReports(std::shared_ptr<const std::vector<CompareReportGroup>> reports) {
    reports_ = std::move(reports);
    if (!reports_) {
        reports_ = std::make_shared<const std::vector<CompareReportGroup>>();
    }
    if(current_group_>=reports_->size()) current_group_=0;
}

void CompareGridTable::SetCurrentGroup(std::size_t group) {
    if(group>=reports_->size()) throw std::out_of_range("Comparison report group");
    current_group_=group;
}

} // namespace adayo::ui
