#include "services/CorpusViewService.h"

#include <algorithm>
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
            throw std::invalid_argument("无法编辑 synthetic blank 播放单元格");
        } else {
            auto segments = ViewBuilder::SplitDisplaySegments(source_cell);
            const auto old_size = segments.size();
            const auto segment_index = *segment_it->second;
            if (segment_index >= segments.size()) {
                segments.resize(segment_index + 1);
            }
            const bool changed = segments[segment_index] != value;
            segments[segment_index] = value;
            source_cell = JoinSegments(std::move(segments));
            const auto new_segments = ViewBuilder::SplitDisplaySegments(source_cell);
            if (changed) {
                InvalidateResultSegment(session, meta.raw_row_index, view_column.source_index, segment_index);
            }
            if (new_segments.size() == old_size) {
                Rebuild(session);
                return;
            }
            InvalidateResultsFromSegment(session, meta.raw_row_index, view_column.source_index, segment_index);
            Rebuild(session);
            return;
        }
    } else {
        const bool changed = source_cell != value;
        source_cell = value;
        if (changed && view_column.role == ColumnRole::Reference) {
            InvalidateResultsForRawRow(session, meta.raw_row_index);
        }
        for (std::size_t row = 0; row < session.view.row_meta.size(); ++row) {
            if (session.view.row_meta[row].raw_row_index == meta.raw_row_index) {
                session.view.rows[row][display_column] = value;
            }
        }
        if (changed && view_column.role == ColumnRole::Reference) {
            Rebuild(session);
        }
        return;
    }
}

ResultCycleState CorpusViewService::CycleResult(CorpusSession& session, std::size_t display_row, std::size_t display_column) const {
    if (display_row >= session.view.rows.size() || display_column >= session.view.columns.size()) {
        throw std::out_of_range("结果单元格越界");
    }
    const auto& column = session.view.columns[display_column];
    if (column.role != ColumnRole::Result) {
        throw std::invalid_argument("当前列不是结果列");
    }
    const auto key = ResultKey(session.view, display_row, display_column);
    const auto current = session.result_marks.find(key);
    if (current == session.result_marks.end() || current->second.empty()) {
        session.result_marks[key] = "pass";
        session.view.rows[display_row][display_column] = "✔";
        return ResultCycleState::Ok;
    }
    if (current->second == "pass") {
        session.result_marks[key] = "fail";
        session.view.rows[display_row][display_column] = "×";
        return ResultCycleState::Ng;
    }
    if (IsResultText(current->second)) {
        session.result_marks.erase(key);
        session.view.rows[display_row][display_column].clear();
        return ResultCycleState::Blank;
    }
    session.result_marks.erase(key);
    session.view.rows[display_row][display_column].clear();
    return ResultCycleState::Blank;
}

ResultIdentity CorpusViewService::ResultKey(const RuntimeView& view, std::size_t display_row, std::size_t result_display_column) {
    if (display_row >= view.row_meta.size() || result_display_column >= view.columns.size()) {
        throw std::out_of_range("结果身份越界");
    }
    const auto& column = view.columns[result_display_column];
    if (column.role != ColumnRole::Result) {
        throw std::invalid_argument("当前列不是结果列");
    }
    const auto& meta = view.row_meta[display_row];
    const auto segment = meta.segment_indexes.find(column.source_index);
    if (segment == meta.segment_indexes.end() || !segment->second.has_value()) {
        throw std::invalid_argument("synthetic blank 没有可记录结果身份");
    }
    return {meta.raw_row_index, column.source_index, *segment->second};
}

void CorpusViewService::InvalidateResultsFromSegment(
    CorpusSession& session,
    std::size_t raw_row,
    std::size_t source_column,
    std::size_t first_segment) {

    for (auto it = session.result_marks.begin(); it != session.result_marks.end();) {
        const auto& key = it->first;
        if (key.raw_row_index == raw_row && key.play_source_column == source_column && key.segment_index >= first_segment) {
            it = session.result_marks.erase(it);
        } else {
            ++it;
        }
    }
}

void CorpusViewService::InvalidateResultSegment(
    CorpusSession& session,
    std::size_t raw_row,
    std::size_t source_column,
    std::size_t segment) {

    session.result_marks.erase(ResultIdentity{raw_row, source_column, segment});
}

void CorpusViewService::InvalidateResultsForRawRow(
    CorpusSession& session,
    std::size_t raw_row) {

    for (auto it = session.result_marks.begin(); it != session.result_marks.end();) {
        const auto& key = it->first;
        if (key.raw_row_index == raw_row) {
            it = session.result_marks.erase(it);
        } else {
            ++it;
        }
    }
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
