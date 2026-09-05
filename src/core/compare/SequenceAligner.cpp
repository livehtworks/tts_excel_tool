#include "core/compare/SequenceAligner.h"
#include "core/unicode/Utf8.h"
#ifdef ADAYO_HAS_RAPIDFUZZ
#include <rapidfuzz/fuzz.hpp>
#endif

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace adayo {
namespace {
using ScoreMatrix = std::vector<std::vector<double>>;

struct Anchor {
    std::size_t r{};
    std::size_t a{};
    double score{};
};

void ValidateOptions(const SequenceAlignmentOptions& options) {
    auto in_range = [](double value) {
        return std::isfinite(value) && value >= 0.0 && value <= 100.0;
    };
    if (!in_range(options.alignment_threshold) || !in_range(options.anchor_threshold)) {
        throw std::invalid_argument("对齐阈值必须在 0 到 100 之间");
    }
    if (!std::isfinite(options.anchor_uniqueness_margin) || options.anchor_uniqueness_margin < 0.0) {
        throw std::invalid_argument("Anchor 唯一性阈值必须为非负数");
    }
    if (!std::isfinite(options.gap_penalty) || options.gap_penalty < 0.0) {
        throw std::invalid_argument("Gap penalty 必须为非负数");
    }
}

void RequireMatrixBudget(std::size_t ref_count, std::size_t actual_count) {
    if (ref_count == 0 || actual_count == 0) return;
    if (ref_count > std::numeric_limits<std::size_t>::max() / actual_count) {
        throw std::runtime_error("对比数据过大，无法安全估算内存");
    }
    const std::size_t cells = ref_count * actual_count;
    if (cells > std::numeric_limits<std::size_t>::max() / sizeof(double)) {
        throw std::runtime_error("对比数据过大，无法安全估算内存");
    }
    const std::size_t bytes = cells * sizeof(double);
    if (bytes > kCompareMatrixMemoryBudgetBytes) {
        throw std::runtime_error("对比数据过大，预计相似度矩阵超过 512 MiB，请拆分后再对比");
    }
}

ScoreMatrix BuildScores(const std::vector<TextRecord>& ref,
                        const std::vector<TextRecord>& act,
                        const TextSimilarity& sim, CompareExecutionContext& context) {
    std::vector<std::u32string> left,right;
    for(const auto& record:ref) { context.Check(); left.push_back(record.normalized_codepoints.empty() ? unicode::DecodeStrict(record.normalized_text) : record.normalized_codepoints); }
    for(const auto& record:act) { context.Check(); right.push_back(record.normalized_codepoints.empty() ? unicode::DecodeStrict(record.normalized_text) : record.normalized_codepoints); }
    for(const auto& text:left) if(text.size()>CompareExecutionContext::record_codepoints) throw std::runtime_error("Normalized record exceeds 65536 codepoints");
    for(const auto& text:right) if(text.size()>CompareExecutionContext::record_codepoints) throw std::runtime_error("Normalized record exceeds 65536 codepoints");
    ScoreMatrix scores(ref.size(), std::vector<double>(act.size(), 0.0));
    for (std::size_t i = 0; i < ref.size(); ++i) {
        context.Check(i,ref.size());
#ifdef ADAYO_HAS_RAPIDFUZZ
        const rapidfuzz::fuzz::CachedRatio<char32_t> scorer(left[i]);
#endif
        for (std::size_t j = 0; j < act.size(); ++j) {
            if(j%256==0) context.Check();
#ifdef ADAYO_HAS_RAPIDFUZZ
            scores[i][j] = scorer.similarity(right[j]);
#else
            scores[i][j] = sim.Ratio(std::u32string_view(left[i]),std::u32string_view(right[j]),&context);
#endif
        }
    }
    return scores;
}

std::vector<Anchor> FindMonotonicAnchors(const ScoreMatrix& scores,
                                         const SequenceAlignmentOptions& options, CompareExecutionContext& context) {
    if (scores.empty() || scores.front().empty()) return {};
    const std::size_t n = scores.size();
    const std::size_t m = scores.front().size();
    const double effective_anchor_threshold = (std::max)(options.anchor_threshold, options.alignment_threshold);

    std::vector<std::size_t> best_ref_for_actual(m, 0);
    std::vector<double> actual_margins(m,0);
    for (std::size_t j = 0; j < m; ++j) {
        context.Check();
        double best = -1.0, second = -1.0;
        for (std::size_t i = 0; i < n; ++i) {
            if(i%1024==0) context.Check();
            if (scores[i][j] > best) { second=best; best = scores[i][j]; best_ref_for_actual[j] = i; }
            else second=(std::max)(second,scores[i][j]);
        }
        actual_margins[j]=second<0 ? best : best-second;
    }

    std::vector<Anchor> candidates;
    for (std::size_t i = 0; i < n; ++i) {
        context.Check();
        double best = -1.0, second = -1.0;
        std::size_t best_j = 0;
        for (std::size_t j = 0; j < m; ++j) {
            const double s = scores[i][j];
            if (s > best) { second = best; best = s; best_j = j; }
            else if (s > second) { second = s; }
        }
        const double margin = second < 0.0 ? best : best - second;
        if (best >= effective_anchor_threshold &&
            margin >= options.anchor_uniqueness_margin &&
            actual_margins[best_j] >= options.anchor_uniqueness_margin &&
            (second<0 || best>second) && actual_margins[best_j]>0 &&
            best_ref_for_actual[best_j] == i) {
            candidates.push_back({i, best_j, best});
        }
    }

    if (candidates.empty()) return {};

    // Longest increasing subsequence on actual index. O(k^2), k <= corpus row count.
    std::vector<std::size_t> dp(candidates.size(), 1), prev(candidates.size(), candidates.size());
    std::size_t best_end = 0;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        context.Check();
        for (std::size_t j = 0; j < i; ++j) {
            if (candidates[j].a < candidates[i].a && dp[j] + 1 > dp[i]) {
                dp[i] = dp[j] + 1;
                prev[i] = j;
            }
        }
        if (dp[i] > dp[best_end]) best_end = i;
    }

    std::vector<Anchor> anchors;
    for (std::size_t cur = best_end;;) {
        anchors.push_back(candidates[cur]);
        if (prev[cur] == candidates.size()) break;
        cur = prev[cur];
    }
    std::reverse(anchors.begin(), anchors.end());
    return anchors;
}

std::vector<AlignmentPair> AlignSegment(const ScoreMatrix& scores,
                                        std::size_t r0, std::size_t r1,
                                        std::size_t a0, std::size_t a1,
                                        const SequenceAlignmentOptions& options, CompareExecutionContext& context) {
    const std::size_t n = r1 - r0;
    const std::size_t m = a1 - a0;
    constexpr double kInf = std::numeric_limits<double>::infinity();
    auto memory=context.Reserve(CompareExecutionContext::Add(
        CompareExecutionContext::Multiply(CompareExecutionContext::Multiply(n+1,m+1),sizeof(double)+sizeof(char)),
        CompareExecutionContext::Multiply(n+m+2,128)),"alignment DP, trace and segment output");

    std::vector<std::vector<double>> dp(n + 1, std::vector<double>(m + 1, kInf));
    std::vector<std::vector<char>> op(n + 1, std::vector<char>(m + 1, 0));
    dp[0][0] = 0.0;
    for (std::size_t i = 1; i <= n; ++i) { dp[i][0] = dp[i - 1][0] + options.gap_penalty; op[i][0] = 'R'; }
    for (std::size_t j = 1; j <= m; ++j) { dp[0][j] = dp[0][j - 1] + options.gap_penalty; op[0][j] = 'A'; }

    for (std::size_t i = 1; i <= n; ++i) {
        context.Check(i,n);
        for (std::size_t j = 1; j <= m; ++j) {
            if(j%1024==0) context.Check();
            const double s = scores[r0 + i - 1][a0 + j - 1];
            const double match_cost = s >= options.alignment_threshold ? (100.0 - s) : kInf;
            const double diag = dp[i - 1][j - 1] + match_cost;
            const double skip_ref = dp[i - 1][j] + options.gap_penalty;
            const double skip_actual = dp[i][j - 1] + options.gap_penalty;

            if (diag <= skip_ref && diag <= skip_actual) { dp[i][j] = diag; op[i][j] = 'M'; }
            else if (skip_ref <= skip_actual) { dp[i][j] = skip_ref; op[i][j] = 'R'; }
            else { dp[i][j] = skip_actual; op[i][j] = 'A'; }
        }
    }

    std::vector<AlignmentPair> reversed;
    std::size_t i = n, j = m;
    while (i > 0 || j > 0) {
        context.Check();
        const char choice = op[i][j];
        if (choice == 'M') {
            const std::size_t ri = r0 + i - 1;
            const std::size_t aj = a0 + j - 1;
            reversed.push_back({ri, aj, scores[ri][aj]});
            --i; --j;
        } else if (choice == 'R') {
            reversed.push_back({r0 + i - 1, std::nullopt, 0.0});
            --i;
        } else {
            reversed.push_back({std::nullopt, a0 + j - 1, 0.0});
            --j;
        }
    }
    std::reverse(reversed.begin(), reversed.end());
    return reversed;
}
}

SequenceAligner::SequenceAligner(TextSimilarity similarity) : similarity_(std::move(similarity)) {}

std::vector<AlignmentPair> SequenceAligner::Align(
    const std::vector<TextRecord>& reference,
    const std::vector<TextRecord>& actual,
    const SequenceAlignmentOptions& options, CompareExecutionContext* context) const {
    CompareExecutionContext local; if(!context) context=&local; context->Check();
    ValidateOptions(options);

    if (reference.empty()) {
        auto memory=context->Reserve(CompareExecutionContext::Multiply(actual.size(),sizeof(AlignmentPair)),"unpaired actual output");
        std::vector<AlignmentPair> out;
        out.reserve(actual.size());
        for (std::size_t j = 0; j < actual.size(); ++j) { context->Check(); out.push_back({std::nullopt, j, 0.0}); }
        return out;
    }
    if (actual.empty()) {
        auto memory=context->Reserve(CompareExecutionContext::Multiply(reference.size(),sizeof(AlignmentPair)),"unpaired reference output");
        std::vector<AlignmentPair> out;
        out.reserve(reference.size());
        for (std::size_t i = 0; i < reference.size(); ++i) { context->Check(); out.push_back({i, std::nullopt, 0.0}); }
        return out;
    }

    RequireMatrixBudget(reference.size(), actual.size());
    std::size_t text_bytes=0;
    for(const auto& record:reference) text_bytes=CompareExecutionContext::Add(text_bytes,record.normalized_text.size());
    for(const auto& record:actual) text_bytes=CompareExecutionContext::Add(text_bytes,record.normalized_text.size());
    auto memory=context->Reserve(CompareExecutionContext::Add(
        CompareExecutionContext::Multiply(CompareExecutionContext::Multiply(reference.size(),actual.size()),sizeof(double)),
        CompareExecutionContext::Add(CompareExecutionContext::Multiply(text_bytes,16),CompareExecutionContext::Multiply(reference.size()+actual.size()+2,256))),"scores, predecoded Unicode, anchors and output");
    // Preflight the worst simultaneous DP allocation before scoring begins.
    { auto peak=context->Reserve(CompareExecutionContext::Add(
        CompareExecutionContext::Multiply(CompareExecutionContext::Multiply(reference.size()+1,actual.size()+1),9),
        CompareExecutionContext::Multiply(reference.size()+actual.size()+2,128)),"peak scores + DP + trace"); }
    const auto scores = BuildScores(reference, actual, similarity_,*context);
    const auto anchors = FindMonotonicAnchors(scores, options,*context);

    std::vector<AlignmentPair> result;
    std::size_t r_cursor = 0, a_cursor = 0;
    for (const auto& anchor : anchors) {
        context->Check();
        auto segment = AlignSegment(scores, r_cursor, anchor.r, a_cursor, anchor.a, options,*context);
        result.insert(result.end(), segment.begin(), segment.end());
        result.push_back({anchor.r, anchor.a, anchor.score});
        r_cursor = anchor.r + 1;
        a_cursor = anchor.a + 1;
    }
    auto tail = AlignSegment(scores, r_cursor, reference.size(), a_cursor, actual.size(), options,*context);
    result.insert(result.end(), tail.begin(), tail.end());
    return result;
}

} // namespace adayo
