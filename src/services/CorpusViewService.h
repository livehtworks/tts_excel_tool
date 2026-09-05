#pragma once

#include "core/workbook/ViewBuilder.h"

#include <optional>
#include <string>
#include <unordered_map>

namespace adayo {

enum class ResultCycleState {
    Blank,
    Ok,
    Ng,
};

struct CorpusSession {
    std::vector<std::vector<std::string>> source_rows;
    std::vector<SelectedColumn> selected_columns;
    RuntimeView view;
    std::unordered_map<ResultIdentity, std::string, ResultIdentityHash> result_marks;
};

class CorpusViewService {
public:
    CorpusSession CreateSession(
        std::vector<std::vector<std::string>> source_rows,
        std::vector<SelectedColumn> selected_columns) const;

    void UpdateDisplayCell(CorpusSession& session, std::size_t display_row, std::size_t display_column, const std::string& value) const;
    ResultCycleState CycleResult(CorpusSession& session, std::size_t display_row, std::size_t display_column) const;
    void Rebuild(CorpusSession& session) const;

private:
    static ResultIdentity ResultKey(const RuntimeView& view, std::size_t display_row, std::size_t result_display_column);
    static void InvalidateResultSegment(CorpusSession& session, std::size_t raw_row, std::size_t source_column, std::size_t segment);
    static void InvalidateResultsForRawRow(CorpusSession& session, std::size_t raw_row);
    static void InvalidateResultsFromSegment(CorpusSession& session, std::size_t raw_row, std::size_t source_column, std::size_t first_segment);
    static std::string JoinSegments(std::vector<std::string> segments);
    ViewBuilder builder_;
};

} // namespace adayo
