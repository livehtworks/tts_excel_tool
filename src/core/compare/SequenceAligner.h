#pragma once

#include "core/domain/Types.h"
#include "core/compare/TextSimilarity.h"

#include <cstddef>
#include <vector>

namespace adayo {

struct SequenceAlignmentOptions {
    double alignment_threshold{80.0};
    double anchor_threshold{95.0};
    double anchor_uniqueness_margin{5.0};
    double gap_penalty{45.0};
};

constexpr std::size_t kCompareMatrixMemoryBudgetBytes = 512ull * 1024ull * 1024ull;

class SequenceAligner {
public:
    explicit SequenceAligner(TextSimilarity similarity = {});

    std::vector<AlignmentPair> Align(
        const std::vector<TextRecord>& reference,
        const std::vector<TextRecord>& actual,
        const SequenceAlignmentOptions& options = {}) const;

private:
    TextSimilarity similarity_;
};

} // namespace adayo
