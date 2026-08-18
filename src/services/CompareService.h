#pragma once

#include "core/compare/CharacterDiff.h"
#include "core/compare/SequenceAligner.h"
#include "core/compare/TextNormalizer.h"
#include "core/domain/Types.h"

#include <string>
#include <vector>

namespace adayo {

struct CompareOptions {
    NormalizerOptions normalizer;
    SequenceAlignmentOptions alignment;
    double pass_threshold{100.0};
};

class CompareService {
public:
    std::vector<CompareRow> Compare(
        const std::vector<std::string>& reference,
        const std::vector<std::string>& actual,
        const CompareOptions& options = {}) const;
};

} // namespace adayo
