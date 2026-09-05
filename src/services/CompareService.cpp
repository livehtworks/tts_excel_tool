#include "services/CompareService.h"

#include <cmath>
#include <stdexcept>

namespace adayo {
namespace {

void ValidateCompareOptions(const CompareOptions& options) {
    if (!std::isfinite(options.pass_threshold) || options.pass_threshold < 0.0 || options.pass_threshold > 100.0) {
        throw std::invalid_argument("OK 阈值必须在 0 到 100 之间");
    }
    if (options.alignment.alignment_threshold > options.pass_threshold) {
        throw std::invalid_argument("对齐阈值不能大于 OK 阈值");
    }
}

} // namespace

std::vector<CompareRow> CompareService::Compare(
    const std::vector<std::string>& reference,
    const std::vector<std::string>& actual,
    const CompareOptions& options) const {
    ValidateCompareOptions(options);

    const TextNormalizer normalizer(options.normalizer);
    std::vector<TextRecord> ref_records;
    std::vector<TextRecord> act_records;
    ref_records.reserve(reference.size());
    act_records.reserve(actual.size());

    for (std::size_t i = 0; i < reference.size(); ++i) {
        ref_records.push_back({i, reference[i], normalizer.Normalize(reference[i])});
    }
    for (std::size_t i = 0; i < actual.size(); ++i) {
        act_records.push_back({i, actual[i], normalizer.Normalize(actual[i])});
    }

    SequenceAligner aligner;
    CharacterDiff differ;
    const auto aligned = aligner.Align(ref_records, act_records, options.alignment);

    std::vector<CompareRow> rows;
    rows.reserve(aligned.size());
    for (const auto& pair : aligned) {
        CompareRow row;
        row.reference_index = pair.reference_index;
        row.actual_index = pair.actual_index;
        row.similarity = pair.similarity;
        if (pair.reference_index) row.reference_text = ref_records[*pair.reference_index].raw_text;
        if (pair.actual_index) row.actual_text = act_records[*pair.actual_index].raw_text;

        if (!pair.reference_index) {
            row.status = CompareStatus::Extra;
        } else if (!pair.actual_index) {
            row.status = CompareStatus::Missing;
        } else {
            row.status = pair.similarity >= options.pass_threshold ? CompareStatus::Ok : CompareStatus::Ng;
            row.diff = differ.Diff(row.reference_text, row.actual_text);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

} // namespace adayo
