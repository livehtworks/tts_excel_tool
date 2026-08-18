#pragma once

#include <string>
#include <string_view>

namespace adayo::unicode {

std::u32string Decode(std::string_view utf8);
std::string Encode(std::u32string_view text);
bool IsWhitespace(char32_t cp);
bool IsPunctuation(char32_t cp);
std::u32string Trim(std::u32string_view text);

} // namespace adayo::unicode
