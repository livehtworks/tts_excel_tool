#pragma once

#include <string>
#include <string_view>
#include "core/domain/Types.h"

namespace adayo {

class TextNormalizer {
public:
    explicit TextNormalizer(NormalizerOptions options = {});
    std::string Normalize(std::string_view raw) const;
    std::u32string NormalizeCodepoints(std::u32string_view raw) const;
    const NormalizerOptions& Options() const noexcept { return options_; }

private:
    NormalizerOptions options_;
};

} // namespace adayo
