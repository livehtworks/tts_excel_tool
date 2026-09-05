#include "core/workbook/ViewBuilder.h"

#include "core/unicode/Utf8.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace adayo {
namespace {
std::string Trim(const std::string& text) {
    return unicode::Encode(unicode::Trim(unicode::Decode(text)));
}

std::string ResultSymbol(const std::string& state) {
    if (state == "pass") return "✔";
    if (state == "fail") return "×";
    return {};
}
}

std::vector<std::string> ViewBuilder::SplitDisplaySegments(const std::string& value) {
    std::vector<std::string> result;
    std::stringstream ss(value);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        line = Trim(line);
        if (!line.empty()) result.push_back(line);
    }
    // std::getline returns no item for a non-empty string without newline only if stream failed unexpectedly.
    if (result.empty()) {
        const auto single = Trim(value);
        if (!single.empty()) result.push_back(single);
    }
    return result;
}

ViewBuilder::BuildResult ViewBuilder::Build(
    const std::vector<std::vector<std::string>>& raw_rows,
    std::vector<SelectedColumn> selected_columns,
    const std::unordered_map<ResultIdentity, std::string, ResultIdentityHash>& result_marks,
    const std::vector<std::size_t>& reference_owners) const {

    selected_columns.erase(
        std::remove_if(selected_columns.begin(), selected_columns.end(),
            [](const SelectedColumn& c) { return c.role == ColumnRole::Ignore; }),
        selected_columns.end());

    const auto ref_count = std::count_if(selected_columns.begin(), selected_columns.end(),
        [](const SelectedColumn& c) { return c.role == ColumnRole::Reference; });
    const auto play_count = std::count_if(selected_columns.begin(), selected_columns.end(),
        [](const SelectedColumn& c) { return c.role == ColumnRole::Play; });
    if (selected_columns.empty()) throw std::invalid_argument("至少要选择一列显示列");
    if (ref_count > 1) throw std::invalid_argument("参考列当前只允许选择 1 列");
    if (play_count == 0) throw std::invalid_argument("至少需要选择 1 列播放列");

    std::stable_sort(selected_columns.begin(), selected_columns.end(), [](const SelectedColumn& a, const SelectedColumn& b) {
        const int ar = a.role == ColumnRole::Reference ? 0 : 1;
        const int br = b.role == ColumnRole::Reference ? 0 : 1;
        return ar != br ? ar < br : a.source_index < b.source_index;
    });

    std::vector<std::vector<std::string>> base_rows;
    std::vector<DisplayRowMeta> row_meta;

    for (std::size_t raw_idx = 0; raw_idx < raw_rows.size(); ++raw_idx) {
        const auto& raw = raw_rows[raw_idx];
        std::unordered_map<std::size_t, std::string> refs;
        std::unordered_map<std::size_t, std::vector<std::string>> plays;
        std::size_t max_lines = 1;

        for (const auto& c : selected_columns) {
            const std::string value = c.source_index < raw.size() ? raw[c.source_index] : std::string{};
            if (c.role == ColumnRole::Reference) {
                const auto owner=raw_idx<reference_owners.size() ? reference_owners[raw_idx] : raw_idx;
                if(owner>=raw_rows.size()) throw std::invalid_argument("Reference owner is out of range");
                refs[c.source_index] = c.source_index<raw_rows[owner].size() ? raw_rows[owner][c.source_index] : std::string{};
            } else if (c.role == ColumnRole::Play) {
                auto segments = SplitDisplaySegments(value);
                max_lines = std::max(max_lines, segments.size());
                plays[c.source_index] = std::move(segments);
            }
        }

        for (std::size_t expanded = 0; expanded < max_lines; ++expanded) {
            std::vector<std::string> row;
            DisplayRowMeta meta{raw_idx, expanded, {}};
            for (const auto& c : selected_columns) {
                if (c.role == ColumnRole::Reference) {
                    row.push_back(refs[c.source_index]);
                } else {
                    const auto& segments = plays[c.source_index];
                    if (expanded < segments.size()) {
                        row.push_back(segments[expanded]);
                        meta.segment_indexes[c.source_index] = expanded;
                    } else {
                        row.emplace_back();
                        meta.segment_indexes[c.source_index] = std::nullopt;
                    }
                }
            }
            const bool empty = std::all_of(row.begin(), row.end(), [](const std::string& s) { return Trim(s).empty(); });
            if (!empty) {
                base_rows.push_back(std::move(row));
                row_meta.push_back(std::move(meta));
            }
        }
    }

    RuntimeView view;
    view.headers.push_back("序号");
    view.columns.push_back({0, "序号", "", ColumnRole::Index, "", ""});
    for (const auto& col : selected_columns) {
        view.headers.push_back(col.header);
        view.columns.push_back(col);
        if (col.role == ColumnRole::Play) {
            view.headers.push_back(col.header + "结果");
            SelectedColumn result_col = col;
            result_col.header += "结果";
            result_col.role = ColumnRole::Result;
            view.columns.push_back(std::move(result_col));
        }
    }

    for (std::size_t r = 0; r < base_rows.size(); ++r) {
        std::vector<std::string> final_row;
        final_row.push_back(std::to_string(r + 1));
        for (std::size_t c = 0; c < selected_columns.size(); ++c) {
            final_row.push_back(base_rows[r][c]);
            if (selected_columns[c].role == ColumnRole::Play) {
                std::string result;
                const auto segment = row_meta[r].segment_indexes.find(selected_columns[c].source_index);
                if (segment != row_meta[r].segment_indexes.end() && segment->second.has_value()) {
                    const ResultIdentity key{row_meta[r].raw_row_index, selected_columns[c].source_index, *segment->second};
                    auto it = result_marks.find(key);
                    if (it != result_marks.end()) result = ResultSymbol(it->second);
                }
                final_row.push_back(result);
            }
        }
        view.rows.push_back(std::move(final_row));
    }
    view.row_meta = std::move(row_meta);

    return {std::move(view), std::move(selected_columns)};
}

} // namespace adayo
