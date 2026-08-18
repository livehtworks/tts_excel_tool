#pragma once

#include "core/domain/Types.h"

#include <string_view>

namespace adayo {

class CharacterDiff {
public:
    CharacterDiffResult Diff(std::string_view reference_utf8, std::string_view actual_utf8) const;
};

} // namespace adayo
