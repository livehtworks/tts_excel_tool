#include "services/CompareService.h"
#include "core/unicode/Utf8.h"
#include "core/compare/TextSimilarity.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace adayo {
namespace {
template<class Sequence>
EditStatistics UnitEdits(const Sequence& reference, const Sequence& actual, CompareExecutionContext& context) {
    EditStatistics result;
    result.reference_units=reference.size();
    std::size_t prefix=0, n=reference.size(), m=actual.size();
    while(prefix<n && prefix<m && reference[prefix]==actual[prefix]) { context.Check(); ++prefix; }
    while(n>prefix && m>prefix && reference[n-1]==actual[m-1]) { context.Check(); --n; --m; }
    n-=prefix; m-=prefix;
    if(!n) { result.insertions=m; return result; }
    if(!m) { result.deletions=n; return result; }
    const auto width=CompareExecutionContext::Add(m,1);
    const auto cells=CompareExecutionContext::Multiply(CompareExecutionContext::Add(n,1),width);
    auto memory=context.Reserve(CompareExecutionContext::Multiply(cells,sizeof(std::uint32_t)),"unit Levenshtein traceback");
    std::vector<std::uint32_t> dp(cells);
    for(std::size_t i=0;i<=n;++i) dp[i*width]=static_cast<std::uint32_t>(i);
    for(std::size_t j=0;j<=m;++j) dp[j]=static_cast<std::uint32_t>(j);
    for(std::size_t i=1;i<=n;++i) {
        context.Check(i,n);
        for(std::size_t j=1;j<=m;++j) {
            if((j&255)==0) context.Check();
            dp[i*width+j]=std::min({dp[(i-1)*width+j]+1,dp[i*width+j-1]+1,
                dp[(i-1)*width+j-1]+(reference[prefix+i-1]==actual[prefix+j-1]?0u:1u)});
        }
    }
    // Stable tie policy v1: match, substitution, deletion, insertion.
    while(n || m) {
        context.Check();
        if(n && m && reference[prefix+n-1]==actual[prefix+m-1] && dp[n*width+m]==dp[(n-1)*width+m-1]) { --n; --m; }
        else if(n && m && dp[n*width+m]==dp[(n-1)*width+m-1]+1) { ++result.substitutions; --n; --m; }
        else if(n && dp[n*width+m]==dp[(n-1)*width+m]+1) { ++result.deletions; --n; }
        else { ++result.insertions; --m; }
    }
    return result;
}
std::vector<std::u32string_view> Words(std::u32string_view text) {
    std::vector<std::u32string_view> result;
    std::size_t begin=0;
    while(begin<text.size()) {
        while(begin<text.size() && unicode::IsWhitespace(text[begin])) ++begin;
        auto end=begin;
        while(end<text.size() && !unicode::IsWhitespace(text[end])) ++end;
        if(end>begin) result.push_back(text.substr(begin,end-begin));
        begin=end;
    }
    return result;
}
}

CompareOptions CompareService::Preset(std::string_view profile) {
    CompareOptions value;
    value.profile_id=profile;
    if(profile=="legacy_v1") return value;
    value.pairing=CompareAlignment::Rows;
    value.normalizer={UnicodeNormalization::None,false,false,false,false};
    if(profile=="strict_rows_v1") value.metric=CompareMetric::Exact;
    else if(profile=="asr_cer_v1") {
        value.metric=CompareMetric::Cer;
        value.normalizer.normalization=UnicodeNormalization::Nfc;
    } else if(profile=="asr_wer_v1") {
        value.metric=CompareMetric::Wer;
        value.normalizer.normalization=UnicodeNormalization::Nfc;
        value.normalizer.case_fold=true;
    } else throw std::invalid_argument("Unknown compare profile");
    return value;
}
void CompareService::ValidateOptions(const CompareOptions& options) {
    const auto preset=Preset(options.profile_id);
    if(options.metric!=preset.metric) throw std::invalid_argument("Metric must match the selected profile");
    auto percentage=[](double value) { return std::isfinite(value) && value>=0 && value<=100; };
    if(!percentage(options.pass_threshold) || !percentage(options.alignment.alignment_threshold) ||
       !percentage(options.alignment.anchor_threshold) || !percentage(options.alignment.anchor_uniqueness_margin) ||
       !std::isfinite(options.alignment.gap_penalty) || options.alignment.gap_penalty<0 ||
       !std::isfinite(options.max_error_rate) || options.max_error_rate<0)
        throw std::invalid_argument("Invalid comparison threshold or error rate");
    if(options.metric==CompareMetric::Indel && options.alignment.alignment_threshold>options.pass_threshold)
        throw std::invalid_argument("Alignment threshold exceeds pass threshold");
    if(options.pairing!=CompareAlignment::Rows && options.pairing!=CompareAlignment::Sequence)
        throw std::invalid_argument("Unknown pairing mode");
    if(options.normalizer.normalization!=UnicodeNormalization::None &&
       options.normalizer.normalization!=UnicodeNormalization::Nfc &&
       options.normalizer.normalization!=UnicodeNormalization::Nfkc)
        throw std::invalid_argument("Unknown Unicode normalization");
    unicode::DecodeStrict(options.delimiter);
}
std::string CompareService::MetricId(CompareMetric metric) {
    switch(metric) {
        case CompareMetric::Indel: return "indel_ratio_v1";
        case CompareMetric::Exact: return "exact_raw_levenshtein_v1";
        case CompareMetric::Cer: return "cer_unit_v1";
        case CompareMetric::Wer: return "wer_unicode_space_v1";
    }
    throw std::invalid_argument("Unknown compare metric");
}
std::string CompareService::ValueLabel(CompareMetric metric) {
    switch(metric) {
        case CompareMetric::Indel: return "Indel相似度(%)";
        case CompareMetric::Exact: return "Levenshtein相似度(%)";
        case CompareMetric::Cer: return "字错误率(%)";
        case CompareMetric::Wer: return "词错误率(%)";
    }
    throw std::invalid_argument("Unknown compare metric");
}
EditStatistics CompareService::Totals(const std::vector<CompareRow>& rows) {
    EditStatistics total;
    for(const auto& row:rows) {
        total.substitutions=CompareExecutionContext::Add(total.substitutions,row.edits.substitutions);
        total.deletions=CompareExecutionContext::Add(total.deletions,row.edits.deletions);
        total.insertions=CompareExecutionContext::Add(total.insertions,row.edits.insertions);
        total.reference_units=CompareExecutionContext::Add(total.reference_units,row.edits.reference_units);
    }
    return total;
}

std::vector<CompareRow> CompareService::Compare(
    const std::vector<std::string>& reference, const std::vector<std::string>& actual,
    const CompareOptions& options, CompareExecutionContext* context) const {
    CompareExecutionContext local;
    if(!context) context=&local;
    context->Check();
    ValidateOptions(options);
    std::size_t bytes=0;
    for(const auto* side:{&reference,&actual}) {
        std::size_t side_bytes=0;
        for(const auto& text:*side) {
            context->Check();
            side_bytes=CompareExecutionContext::Add(side_bytes,text.size());
            if(side_bytes>CompareExecutionContext::file_bytes) throw std::runtime_error("Compare input exceeds 64MiB per side");
        }
        bytes=CompareExecutionContext::Add(bytes,side_bytes);
    }
    const auto count=CompareExecutionContext::Add(CompareExecutionContext::Add(reference.size(),actual.size()),2);
    auto memory=context->Reserve(CompareExecutionContext::Add(CompareExecutionContext::Multiply(bytes,96),
        CompareExecutionContext::Multiply(count,512)),"input snapshots, normalization, report rows and diff fragments");
    const TextNormalizer normalizer(options.normalizer);
    auto records=[&](const auto& input) {
        std::vector<TextRecord> output;
        output.reserve(input.size());
        for(std::size_t i=0;i<input.size();++i) {
            context->Check();
            TextRecord record;
            record.source_index=i;
            record.raw_text=input[i];
            record.raw_codepoints=unicode::DecodeStrict(input[i]);
            if(record.raw_codepoints.size()>CompareExecutionContext::record_codepoints)
                throw std::runtime_error("Compare record exceeds 65536 codepoints");
            record.normalized_codepoints=normalizer.NormalizeCodepoints(record.raw_codepoints);
            record.normalized_text=unicode::Encode(record.normalized_codepoints);
            if(record.normalized_codepoints.size()>CompareExecutionContext::record_codepoints)
                throw std::runtime_error("Normalized record exceeds 65536 codepoints");
            output.push_back(std::move(record));
        }
        return output;
    };
    const auto refs=records(reference), acts=records(actual);
    std::vector<AlignmentPair> aligned;
    if(options.pairing==CompareAlignment::Sequence) {
        aligned=SequenceAligner{}.Align(refs,acts,options.alignment,context);
    } else {
        aligned.reserve(std::max(refs.size(),acts.size()));
        for(std::size_t i=0;i<std::max(refs.size(),acts.size());++i) {
            context->Check();
            AlignmentPair pair;
            if(i<refs.size()) pair.reference_index=i;
            if(i<acts.size()) pair.actual_index=i;
            aligned.push_back(pair);
        }
    }
    std::vector<CompareRow> rows;
    rows.reserve(aligned.size());
    const std::u32string empty;
    for(const auto& pair:aligned) {
        context->Check();
        CompareRow row;
        row.reference_index=pair.reference_index;
        row.actual_index=pair.actual_index;
        row.metric=options.metric;
        const auto* ref=pair.reference_index ? &refs[*pair.reference_index] : nullptr;
        const auto* act=pair.actual_index ? &acts[*pair.actual_index] : nullptr;
        if(ref) { row.reference_text=ref->raw_text; row.reference_length=ref->raw_codepoints.size(); }
        if(act) { row.actual_text=act->raw_text; row.actual_length=act->raw_codepoints.size(); }
        const auto& r=ref ? (options.metric==CompareMetric::Exact?ref->raw_codepoints:ref->normalized_codepoints):empty;
        const auto& a=act ? (options.metric==CompareMetric::Exact?act->raw_codepoints:act->normalized_codepoints):empty;
        if(options.metric==CompareMetric::Wer) row.edits=UnitEdits(Words(r),Words(a),*context);
        else if(options.metric!=CompareMetric::Indel) row.edits=UnitEdits(r,a,*context);
        if(options.metric==CompareMetric::Indel) {
            row.similarity=ref && act ? TextSimilarity{}.Ratio(r,a,context):0;
        } else {
            row.error_rate=row.edits.ErrorRate();
            const auto length=std::max(r.size(),a.size());
            const auto distance=row.edits.substitutions+row.edits.deletions+row.edits.insertions;
            row.similarity=length?100.0*(1.0-static_cast<double>(distance)/length):100.0;
        }
        if(!ref) row.status=CompareStatus::Extra;
        else if(!act) row.status=CompareStatus::Missing;
        else if(options.metric==CompareMetric::Indel) row.status=row.similarity>=options.pass_threshold?CompareStatus::Ok:CompareStatus::Ng;
        else if(options.metric==CompareMetric::Exact) row.status=ref->raw_codepoints==act->raw_codepoints?CompareStatus::Ok:CompareStatus::Ng;
        else row.status=row.error_rate && *row.error_rate<=options.max_error_rate?CompareStatus::Ok:CompareStatus::Ng;
        row.diff=CharacterDiff{}.Diff(ref?ref->raw_codepoints:empty,act?act->raw_codepoints:empty,context);
        rows.push_back(std::move(row));
    }
    return rows;
}
} // namespace adayo
