#pragma once

#include "core/compare/CharacterDiff.h"
#include "core/compare/SequenceAligner.h"
#include "core/compare/TextNormalizer.h"
#include "core/domain/Types.h"

#include <string>
#include <vector>

namespace adayo {

class CompareService {
public:
    static CompareOptions Preset(std::string_view profile);
    static void ValidateOptions(const CompareOptions& options);
    static std::string MetricId(CompareMetric metric);
    static std::string ValueLabel(CompareMetric metric);
    static EditStatistics Totals(const std::vector<CompareRow>& rows);
    std::vector<CompareRow> Compare(
        const std::vector<std::string>& reference,
        const std::vector<std::string>& actual,
        const CompareOptions& options = {}, CompareExecutionContext* context = nullptr) const;
};

} // namespace adayo
