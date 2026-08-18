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
    std::unordered_map<std::string, std::string> result_marks;
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
    static std::string ResultKey(std::size_t display_row, std::size_t source_column);
    static std::string JoinSegments(std::vector<std::string> segments);
    ViewBuilder builder_;
};

} // namespace adayo
