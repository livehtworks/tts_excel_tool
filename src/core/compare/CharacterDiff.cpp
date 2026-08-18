#include "core/compare/CharacterDiff.h"

#include "core/unicode/Utf8.h"

#include <vector>

namespace adayo {
namespace {
void Append(std::vector<DiffFragment>& fragments, char32_t cp, DiffKind kind) {
    const std::string encoded = unicode::Encode(std::u32string(1, cp));
    if (!fragments.empty() && fragments.back().kind == kind) {
        fragments.back().text += encoded;
    } else {
        fragments.push_back({encoded, kind});
    }
}
}

CharacterDiffResult CharacterDiff::Diff(std::string_view reference_utf8, std::string_view actual_utf8) const {
    const auto a = unicode::Decode(reference_utf8);
    const auto b = unicode::Decode(actual_utf8);
    const std::size_t n = a.size();
    const std::size_t m = b.size();

    std::vector<std::vector<std::uint32_t>> lcs(n + 1, std::vector<std::uint32_t>(m + 1, 0));
    for (std::size_t i = n; i-- > 0;) {
        for (std::size_t j = m; j-- > 0;) {
            if (a[i] == b[j]) {
                lcs[i][j] = 1U + lcs[i + 1][j + 1];
            } else {
                lcs[i][j] = std::max(lcs[i + 1][j], lcs[i][j + 1]);
            }
        }
    }

    CharacterDiffResult result;
    std::size_t i = 0, j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && a[i] == b[j]) {
            Append(result.reference_fragments, a[i], DiffKind::Same);
            Append(result.actual_fragments, b[j], DiffKind::Same);
            ++i; ++j;
        } else if (i < n && (j == m || lcs[i + 1][j] >= lcs[i][j + 1])) {
            Append(result.reference_fragments, a[i], DiffKind::Changed);
            ++i;
        } else if (j < m) {
            Append(result.actual_fragments, b[j], DiffKind::Changed);
            ++j;
        }
    }
    return result;
}

} // namespace adayo
