#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace adayo {

enum class ColumnRole {
    Ignore,
    Reference,
    Play,
    Result,
    Index,
};

enum class SuggestedColumnType {
    Unknown,
    Meta,
    Utterance,
    Result,
};

enum class LanguageSelectionMode {
    Auto,
    Fixed,
};

enum class CompareStatus {
    Ok,
    Ng,
    Missing,
    Extra,
};

enum class DiffKind {
    Same,
    Changed,
};
enum class UnicodeNormalization { None, Nfc, Nfkc };
struct NormalizerOptions {
    UnicodeNormalization normalization{UnicodeNormalization::Nfkc};
    bool case_fold{true}, collapse_whitespace{true}, trim{true}, ignore_punctuation{false};
    bool operator==(const NormalizerOptions&) const = default;
};
struct SequenceAlignmentOptions {
    double alignment_threshold{80.0}, anchor_threshold{95.0}, anchor_uniqueness_margin{5.0}, gap_penalty{45.0};
    bool operator==(const SequenceAlignmentOptions&) const = default;
};
enum class CompareMetric { Indel, Exact, Cer, Wer };
enum class CompareAlignment { Sequence, Rows };
struct CompareOptions {
    NormalizerOptions normalizer{UnicodeNormalization::Nfkc,true,true,true,true};
    SequenceAlignmentOptions alignment;
    double pass_threshold{100.0};
    std::string profile_id{"legacy_v1"};
    CompareMetric metric{CompareMetric::Indel};
    CompareAlignment pairing{CompareAlignment::Sequence};
    double max_error_rate{0.0};
    bool custom{false};
    std::string delimiter;
    bool operator==(const CompareOptions&) const = default;
};
struct EditStatistics {
    std::size_t substitutions{}, deletions{}, insertions{}, reference_units{};
    std::optional<double> ErrorRate() const {
        const auto errors=substitutions+deletions+insertions;
        if(reference_units) return static_cast<double>(errors)/static_cast<double>(reference_units);
        return errors ? std::nullopt : std::optional<double>{0.0};
    }
};
struct TextInputSource { std::string path, sha256; };

struct LanguageOption {
    std::string code;
    std::string name;
};

struct ColumnProfile {
    std::size_t source_index{};
    std::string excel_column;
    std::string header;
    std::size_t non_empty_count{};
    std::vector<std::string> samples;
    SuggestedColumnType suggested_type{SuggestedColumnType::Unknown};
    std::string guessed_language;
    bool selected{false};
    ColumnRole role{ColumnRole::Ignore};
    std::string language_code;
    bool language_user_overridden{false};
    LanguageSelectionMode language_selection_mode{LanguageSelectionMode::Auto};
    std::string tts_engine_id{"sherpa-vits"};
    std::string tts_model_id;
};

struct SelectedColumn {
    std::size_t source_index{};
    std::string header;
    std::string excel_column;
    ColumnRole role{ColumnRole::Ignore};
    std::string language_code;
    std::string tts_engine_id{"sherpa-vits"};
    std::string tts_model_id;
};

struct ResultIdentity {
    std::size_t raw_row_index{};
    std::size_t play_source_column{};
    std::size_t segment_index{};

    friend bool operator==(const ResultIdentity& left, const ResultIdentity& right) noexcept {
        return left.raw_row_index == right.raw_row_index &&
               left.play_source_column == right.play_source_column &&
               left.segment_index == right.segment_index;
    }
};

struct ResultIdentityHash {
    std::size_t operator()(const ResultIdentity& identity) const noexcept {
        std::size_t seed = identity.raw_row_index + 0x9e3779b97f4a7c15ull;
        seed ^= identity.play_source_column + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
        seed ^= identity.segment_index + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
        return seed;
    }
};

struct DisplayRowMeta {
    std::size_t raw_row_index{};
    std::size_t expanded_index{};
    std::unordered_map<std::size_t, std::optional<std::size_t>> segment_indexes;
    std::size_t source_excel_row{};
    std::optional<std::size_t> reference_owner_raw_row;
    std::optional<std::size_t> reference_owner_excel_row;
};

struct WorkbookSource {
    std::string path, identity, sha256, sheet, imported_at;
    std::size_t header_row{1};
};
struct MergedRange {
    std::size_t first_row{}, last_row{}, first_column{}, last_column{};
    std::string reference;
};

struct RuntimeView {
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
    std::vector<SelectedColumn> columns;
    std::vector<DisplayRowMeta> row_meta;
    WorkbookSource source;
    std::vector<std::string> diagnostics;
};

struct TextRecord {
    std::size_t source_index{};
    std::string raw_text;
    std::string normalized_text;
    std::u32string raw_codepoints, normalized_codepoints;
};

struct DiffFragment {
    std::string text;
    DiffKind kind{DiffKind::Same};
};

struct CharacterDiffResult {
    std::vector<DiffFragment> reference_fragments;
    std::vector<DiffFragment> actual_fragments;
};

struct AlignmentPair {
    std::optional<std::size_t> reference_index;
    std::optional<std::size_t> actual_index;
    double similarity{};
};

struct CompareRow {
    std::optional<std::size_t> reference_index;
    std::optional<std::size_t> actual_index;
    std::string reference_text;
    std::string actual_text;
    double similarity{};
    CompareStatus status{CompareStatus::Ng};
    CharacterDiffResult diff;
    CompareMetric metric{CompareMetric::Indel};
    EditStatistics edits;
    std::optional<double> error_rate;
    std::size_t reference_length{}, actual_length{};
};

struct CompareReportGroup {
    std::string label;
    std::vector<CompareRow> rows;
    CompareOptions options;
    TextInputSource reference_source, actual_source;
    EditStatistics totals;
};

struct AudioBuffer {
    std::vector<float> samples;
    std::int32_t sample_rate{};
    std::int32_t channels{1};
};

struct TtsModelConfig {
    std::string engine_id;
    std::string model_path;
    std::string tokens_path;
    std::string data_dir;
    std::string lexicon_path;
    std::string rule_fsts;
    std::string language_code;
    std::int32_t speaker_id{0};
    std::int32_t num_threads{2};
};

struct TtsRequest {
    std::string text;
    std::string language_code;
    std::int32_t speaker_id{0};
    double speed{1.0};
};

} // namespace adayo
