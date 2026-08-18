#include "services/CorpusViewService.h"

#include <stdexcept>

namespace adayo {
namespace {
bool IsResultText(const std::string& value) {
    return value == "pass" || value == "fail";
}
} // namespace

CorpusSession CorpusViewService::CreateSession(
    std::vector<std::vector<std::string>> source_rows,
    std::vector<SelectedColumn> selected_columns) const {

    CorpusSession session;
    session.source_rows = std::move(source_rows);
    session.selected_columns = std::move(selected_columns);
    Rebuild(session);
    return session;
}

void CorpusViewService::Rebuild(CorpusSession& session) const {
    auto built = builder_.Build(session.source_rows, session.selected_columns, session.result_marks);
    session.view = std::move(built.view);
    session.selected_columns = std::move(built.selected_columns);
}

void CorpusViewService::UpdateDisplayCell(
    CorpusSession& session,
    std::size_t display_row,
    std::size_t display_column,
    const std::string& value) const {

    if (display_row >= session.view.row_meta.size()) {
        throw std::out_of_range("显示行越界");
    }
    if (display_column >= session.view.columns.size()) {
        throw std::out_of_range("显示列越界");
    }

    const auto& view_column = session.view.columns[display_column];
    const auto& meta = session.view.row_meta[display_row];
    if (meta.raw_row_index >= session.source_rows.size()) {
        throw std::out_of_range("源行越界");
    }
    if (view_column.role == ColumnRole::Index || view_column.role == ColumnRole::Result) {
        return;
    }
    if (view_column.source_index >= session.source_rows[meta.raw_row_index].size()) {
        session.source_rows[meta.raw_row_index].resize(view_column.source_index + 1);
    }

    auto& source_cell = session.source_rows[meta.raw_row_index][view_column.source_index];
    if (view_column.role == ColumnRole::Play) {
        auto segment_it = meta.segment_indexes.find(view_column.source_index);
        if (segment_it == meta.segment_indexes.end() || !segment_it->second.has_value()) {
            if (!value.empty()) {
                auto segments = ViewBuilder::SplitDisplaySegments(source_cell);
                segments.push_back(value);
                source_cell = JoinSegments(std::move(segments));
            }
        } else {
            auto segments = ViewBuilder::SplitDisplaySegments(source_cell);
            const auto segment_index = *segment_it->second;
            if (segment_index >= segments.size()) {
                segments.resize(segment_index + 1);
            }
            segments[segment_index] = value;
            source_cell = JoinSegments(std::move(segments));
        }
    } else {
        source_cell = value;
    }

    Rebuild(session);
}

ResultCycleState CorpusViewService::CycleResult(CorpusSession& session, std::size_t display_row, std::size_t display_column) const {
    if (display_row >= session.view.rows.size() || display_column >= session.view.columns.size()) {
        throw std::out_of_range("结果单元格越界");
    }
    const auto& column = session.view.columns[display_column];
    if (column.role != ColumnRole::Result) {
        throw std::invalid_argument("当前列不是结果列");
    }
    const auto key = ResultKey(display_row, column.source_index);
    const auto current = session.result_marks.find(key);
    if (current == session.result_marks.end() || current->second.empty()) {
        session.result_marks[key] = "pass";
        Rebuild(session);
        return ResultCycleState::Ok;
    }
    if (current->second == "pass") {
        session.result_marks[key] = "fail";
        Rebuild(session);
        return ResultCycleState::Ng;
    }
    if (IsResultText(current->second)) {
        session.result_marks.erase(key);
        Rebuild(session);
        return ResultCycleState::Blank;
    }
    session.result_marks.erase(key);
    Rebuild(session);
    return ResultCycleState::Blank;
}

std::string CorpusViewService::ResultKey(std::size_t display_row, std::size_t source_column) {
    return std::to_string(display_row) + "|" + std::to_string(source_column);
}

std::string CorpusViewService::JoinSegments(std::vector<std::string> segments) {
    std::string joined;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (i > 0) joined.push_back('\n');
        joined += segments[i];
    }
    return joined;
}

} // namespace adayo
