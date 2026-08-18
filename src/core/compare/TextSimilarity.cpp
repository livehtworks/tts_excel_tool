#include "core/compare/TextSimilarity.h"

#include "core/unicode/Utf8.h"

#include <algorithm>
#include <vector>

#ifdef ADAYO_HAS_RAPIDFUZZ
#include <rapidfuzz/fuzz.hpp>
#endif

namespace adayo {
namespace {
std::size_t Levenshtein(std::u32string_view a, std::u32string_view b) {
    if (a.size() > b.size()) return Levenshtein(b, a);
    std::vector<std::size_t> prev(a.size() + 1), cur(a.size() + 1);
    for (std::size_t i = 0; i <= a.size(); ++i) prev[i] = i;
    for (std::size_t j = 1; j <= b.size(); ++j) {
        cur[0] = j;
        for (std::size_t i = 1; i <= a.size(); ++i) {
            const std::size_t subst = prev[i - 1] + (a[i - 1] == b[j - 1] ? 0U : 1U);
            cur[i] = std::min({cur[i - 1] + 1U, prev[i] + 1U, subst});
        }
        std::swap(prev, cur);
    }
    return prev[a.size()];
}
}

double TextSimilarity::Ratio(std::string_view left_utf8, std::string_view right_utf8) const {
    const auto left = unicode::Decode(left_utf8);
    const auto right = unicode::Decode(right_utf8);
    if (left.empty() && right.empty()) return 100.0;
    if (left.empty() || right.empty()) return 0.0;
#ifdef ADAYO_HAS_RAPIDFUZZ
    return rapidfuzz::fuzz::ratio(left, right);
#else
    const auto dist = Levenshtein(left, right);
    const auto max_len = std::max(left.size(), right.size());
    return 100.0 * (1.0 - static_cast<double>(dist) / static_cast<double>(max_len));
#endif
}

} // namespace adayo
