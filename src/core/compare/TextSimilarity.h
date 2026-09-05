#pragma once

#include <string_view>
#include "core/compare/CompareExecutionContext.h"

namespace adayo {

class TextSimilarity {
public:
    double Ratio(std::string_view left_utf8, std::string_view right_utf8) const;
    double Ratio(std::u32string_view left, std::u32string_view right, CompareExecutionContext* context = nullptr) const;
};

} // namespace adayo
