#include "services/CorpusViewService.h"

#include <algorithm>
#include <stdexcept>
#include <numeric>
#include <map>
#include <atomic>
#include <limits>

namespace adayo {
namespace {
std::uint64_t NextSessionId() {
    static std::atomic<std::uint64_t> next{0};
    return ++next;
}
bool IsResultText(const std::string& value) {
    return value == "pass" || value == "fail";
}
} // namespace

CorpusSession CorpusViewService::CreateSession(
    std::vector<std::vector<std::string>> source_rows,
    std::vector<SelectedColumn> selected_columns) const {

    CorpusSession session;
    session.session_id=NextSessionId();
    session.source_rows = std::move(source_rows);
    session.selected_columns = std::move(selected_columns);
    Rebuild(session);
    return session;
}

CorpusSession CorpusViewService::CreateSessionFromWorksheet(WorksheetData worksheet, std::vector<SelectedColumn> selected_columns) const {
    CorpusSession session;
    session.session_id=NextSessionId();
    session.source_rows=std::move(worksheet.rows); session.selected_columns=std::move(selected_columns);
    session.source_excel_row_numbers=std::move(worksheet.source_excel_row_numbers);
    session.merged_ranges=std::move(worksheet.merged_ranges); session.source=std::move(worksheet.source);
    if(session.source_excel_row_numbers.size()!=session.source_rows.size()) throw std::invalid_argument("Worksheet source row mapping mismatch");
    session.reference_owners.resize(session.source_rows.size());
    std::iota(session.reference_owners.begin(),session.reference_owners.end(),0);
    const auto reference=std::find_if(session.selected_columns.begin(),session.selected_columns.end(),[](const auto& c){return c.role==ColumnRole::Reference;});
    if(reference!=session.selected_columns.end()) {
        const auto column=reference->source_index;
        std::map<std::size_t,std::size_t> indexes;
        for(std::size_t i=0;i<session.source_excel_row_numbers.size();++i) indexes[session.source_excel_row_numbers[i]]=i;
        std::vector<bool> projected(session.source_rows.size());
        std::vector<const MergedRange*> relevant;
        for(const auto& merge:session.merged_ranges)
            if(column+1>=merge.first_column && column+1<=merge.last_column) relevant.push_back(&merge);
        std::sort(relevant.begin(),relevant.end(),[](auto a,auto b){return a->first_row<b->first_row;});
        std::vector<const MergedRange*> conflicts;
        for(std::size_t begin=0;begin<relevant.size();) {
            auto end=begin+1, last=relevant[begin]->last_row;
            while(end<relevant.size() && relevant[end]->first_row<=last) {
                last=std::max(last,relevant[end]->last_row); ++end;
            }
            if(end>begin+1) conflicts.insert(conflicts.end(),relevant.begin()+begin,relevant.begin()+end);
            begin=end;
        }
        for(const auto& merge:session.merged_ranges) {
            if(column+1<merge.first_column || column+1>merge.last_column) continue;
            if(std::find(conflicts.begin(),conflicts.end(),&merge)!=conflicts.end()) {
                session.diagnostics.push_back("Overlapping reference merge not projected: "+merge.reference); continue;
            }
            if(merge.first_column!=merge.last_column) { session.diagnostics.push_back("Cross-column merge not projected: "+merge.reference); continue; }
            const auto anchor=indexes.find(merge.first_row);
            bool valid=anchor!=indexes.end() && merge.first_row>session.source.header_row && merge.first_row<=merge.last_row;
            std::vector<std::size_t> covered;
            const std::string value=valid && column<session.source_rows[anchor->second].size() ? session.source_rows[anchor->second][column] : std::string{};
            if(value.empty()) valid=false;
            for(auto it=indexes.lower_bound(merge.first_row);it!=indexes.end() && it->first<=merge.last_row;++it) {
                const auto row=it->second; covered.push_back(row);
                if(projected[row] || (column<session.source_rows[row].size() && !session.source_rows[row][column].empty() && session.source_rows[row][column]!=value)) valid=false;
            }
            if(!valid) { session.diagnostics.push_back("Invalid reference merge not projected: "+merge.reference); continue; }
            for(auto row:covered) { session.reference_owners[row]=anchor->second; projected[row]=true; }
        }
    }
    Rebuild(session); return session;
}

void CorpusViewService::Rebuild(CorpusSession& session) const {
    auto built = builder_.Build(session.source_rows, session.selected_columns, session.result_marks, session.reference_owners);
    session.view = std::move(built.view);
    session.view.source=session.source; session.view.diagnostics=session.diagnostics;
    for(auto& meta:session.view.row_meta) {
        if(meta.raw_row_index<session.source_excel_row_numbers.size()) meta.source_excel_row=session.source_excel_row_numbers[meta.raw_row_index];
        if(meta.raw_row_index<session.reference_owners.size()) {
            const auto owner=session.reference_owners[meta.raw_row_index]; meta.reference_owner_raw_row=owner;
            if(owner<session.source_excel_row_numbers.size()) meta.reference_owner_excel_row=session.source_excel_row_numbers[owner];
        }
    }
    session.selected_columns = std::move(built.selected_columns);
}

CellEditImpact CorpusViewService::UpdateDisplayCell(
    CorpusSession& session, std::size_t row, std::size_t column, const std::string& value) const {
    auto candidate=session;
    const auto impact=UpdateDisplayCellInPlace(candidate,row,column,value);
    if(candidate.source_rows!=session.source_rows) {
        if(session.revision==std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Session revision exhausted");
        candidate.revision=session.revision+1;
    }
    session=std::move(candidate);
    return impact;
}

bool CorpusViewService::MarkExported(CorpusSession& session, std::uint64_t session_id, std::uint64_t revision) const {
    if(session.session_id!=session_id || revision>session.revision) return false;
    session.exported_revision=std::max(session.exported_revision,revision);
    return true;
}

CellEditImpact CorpusViewService::UpdateDisplayCellInPlace(
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

    if(value.find('\0')!=std::string::npos) throw std::invalid_argument("Embedded NUL in edited text");
    const auto view_column = session.view.columns[display_column];
    const auto meta = session.view.row_meta[display_row];
    CellEditImpact impact;
    impact.raw_rows.push_back(meta.raw_row_index);
    auto finish=[&]() {
        for(std::size_t row=0;row<session.view.row_meta.size();++row)
            if(std::find(impact.raw_rows.begin(),impact.raw_rows.end(),session.view.row_meta[row].raw_row_index)!=impact.raw_rows.end()) impact.display_rows.push_back(row);
        return impact;
    };
    if (meta.raw_row_index >= session.source_rows.size()) {
        throw std::out_of_range("源行越界");
    }
    if (view_column.role == ColumnRole::Index || view_column.role == ColumnRole::Result) {
        throw std::invalid_argument("序号和结果列不能作为文本编辑");
    }
    if (view_column.source_index >= session.source_rows[meta.raw_row_index].size()) {
        session.source_rows[meta.raw_row_index].resize(view_column.source_index + 1);
    }

    const auto owner=view_column.role==ColumnRole::Reference && meta.raw_row_index<session.reference_owners.size()
        ? session.reference_owners[meta.raw_row_index] : meta.raw_row_index;
    if(view_column.source_index>=session.source_rows[owner].size()) session.source_rows[owner].resize(view_column.source_index+1);
    auto& source_cell = session.source_rows[owner][view_column.source_index];
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
                return finish();
            }
            InvalidateResultsFromSegment(session, meta.raw_row_index, view_column.source_index, segment_index);
            impact.segment_structure_changed=true;
            Rebuild(session);
            return finish();
        }
    } else {
        const bool changed = source_cell != value;
        source_cell = value;
        if (changed && view_column.role == ColumnRole::Reference) {
            impact.raw_rows.clear();
            for(std::size_t row=0;row<session.source_rows.size();++row) {
                const auto candidate=row<session.reference_owners.size() ? session.reference_owners[row] : row;
                if(candidate==owner) { InvalidateResultsForRawRow(session,row); impact.raw_rows.push_back(row); }
            }
        }
        for (std::size_t row = 0; row < session.view.row_meta.size(); ++row) {
            if (session.view.row_meta[row].raw_row_index == meta.raw_row_index) {
                session.view.rows[row][display_column] = value;
            }
        }
        if (changed && view_column.role == ColumnRole::Reference) {
            Rebuild(session);
        }
        return finish();
    }
}

ResultCycleState CorpusViewService::CycleResult(CorpusSession& session, std::size_t display_row, std::size_t display_column) const {
    auto candidate=session;
    if(session.revision==std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Session revision exhausted");
    const auto result=CycleResultInPlace(candidate,display_row,display_column);
    candidate.revision=session.revision+1;
    session=std::move(candidate);
    return result;
}

ResultCycleState CorpusViewService::CycleResultInPlace(CorpusSession& session, std::size_t display_row, std::size_t display_column) const {
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
