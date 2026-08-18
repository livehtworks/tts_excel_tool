#pragma once

#include "core/domain/Types.h"

#include <string>
#include <vector>

namespace adayo {

class ColumnAnalyzer {
public:
    std::vector<ColumnProfile> Analyze(
        const std::vector<std::string>& headers,
        const std::vector<std::vector<std::string>>& rows) const;

    std::string GuessLanguage(const std::string& header) const;
    SuggestedColumnType GuessType(const std::string& header) const;

    static const std::vector<LanguageOption>& Languages();
    static std::string ExcelColumnName(std::size_t zero_based_index);
};

} // namespace adayo
