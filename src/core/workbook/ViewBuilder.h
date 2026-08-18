#pragma once

#include "core/domain/Types.h"

#include <unordered_map>
#include <string>
#include <vector>

namespace adayo {

class ViewBuilder {
public:
    struct BuildResult {
        RuntimeView view;
        std::vector<SelectedColumn> selected_columns;
    };

    BuildResult Build(
        const std::vector<std::vector<std::string>>& raw_rows,
        std::vector<SelectedColumn> selected_columns,
        const std::unordered_map<ResultIdentity, std::string, ResultIdentityHash>& result_marks = {}) const;

    static std::vector<std::string> SplitDisplaySegments(const std::string& value);
};

} // namespace adayo
