#pragma once

#include "core/domain/Types.h"
#include "core/compare/CompareExecutionContext.h"

#include <string_view>

namespace adayo {

class CharacterDiff {
public:
    CharacterDiffResult Diff(std::string_view reference_utf8, std::string_view actual_utf8, CompareExecutionContext* context = nullptr) const;
    CharacterDiffResult Diff(std::u32string_view reference, std::u32string_view actual, CompareExecutionContext* context = nullptr) const;
};

} // namespace adayo
