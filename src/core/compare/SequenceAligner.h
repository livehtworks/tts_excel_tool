#pragma once

#include "core/domain/Types.h"
#include "core/compare/TextSimilarity.h"

#include <cstddef>
#include <vector>

namespace adayo {


constexpr std::size_t kCompareMatrixMemoryBudgetBytes = 512ull * 1024ull * 1024ull;

class SequenceAligner {
public:
    explicit SequenceAligner(TextSimilarity similarity = {});

    std::vector<AlignmentPair> Align(
        const std::vector<TextRecord>& reference,
        const std::vector<TextRecord>& actual,
        const SequenceAlignmentOptions& options = {}, CompareExecutionContext* context = nullptr) const;

private:
    TextSimilarity similarity_;
};

} // namespace adayo
