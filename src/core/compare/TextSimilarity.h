#pragma once

#include <string_view>

namespace adayo {

class TextSimilarity {
public:
    double Ratio(std::string_view left_utf8, std::string_view right_utf8) const;
};

} // namespace adayo
