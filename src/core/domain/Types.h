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

struct DisplayRowMeta {
    std::size_t raw_row_index{};
    std::size_t expanded_index{};
    std::unordered_map<std::size_t, std::optional<std::size_t>> segment_indexes;
};

struct RuntimeView {
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
    std::vector<SelectedColumn> columns;
    std::vector<DisplayRowMeta> row_meta;
};

struct TextRecord {
    std::size_t source_index{};
    std::string raw_text;
    std::string normalized_text;
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
