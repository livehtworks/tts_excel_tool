#pragma once

#include <string>
#include <string_view>

namespace adayo {

struct NormalizerOptions {
    bool unicode_nfkc{true};
    bool case_fold{true};
    bool collapse_whitespace{true};
    bool trim{true};
    bool ignore_punctuation{false};
};

class TextNormalizer {
public:
    explicit TextNormalizer(NormalizerOptions options = {});
    std::string Normalize(std::string_view raw) const;
    const NormalizerOptions& Options() const noexcept { return options_; }

private:
    NormalizerOptions options_;
};

} // namespace adayo
