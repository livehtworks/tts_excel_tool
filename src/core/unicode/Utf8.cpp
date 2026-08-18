#include "core/unicode/Utf8.h"

#include <array>

namespace adayo::unicode {
namespace {
constexpr char32_t kReplacement = 0xFFFD;

bool IsContinuation(unsigned char c) {
    return (c & 0xC0U) == 0x80U;
}
}

std::u32string Decode(std::string_view utf8) {
    std::u32string out;
    out.reserve(utf8.size());

    std::size_t i = 0;
    while (i < utf8.size()) {
        const auto c0 = static_cast<unsigned char>(utf8[i]);
        if (c0 < 0x80U) {
            out.push_back(static_cast<char32_t>(c0));
            ++i;
            continue;
        }

        int length = 0;
        char32_t cp = 0;
        char32_t min_cp = 0;
        if ((c0 & 0xE0U) == 0xC0U) {
            length = 2; cp = c0 & 0x1FU; min_cp = 0x80;
        } else if ((c0 & 0xF0U) == 0xE0U) {
            length = 3; cp = c0 & 0x0FU; min_cp = 0x800;
        } else if ((c0 & 0xF8U) == 0xF0U) {
            length = 4; cp = c0 & 0x07U; min_cp = 0x10000;
        } else {
            out.push_back(kReplacement);
            ++i;
            continue;
        }

        if (i + static_cast<std::size_t>(length) > utf8.size()) {
            out.push_back(kReplacement);
            break;
        }

        bool valid = true;
        for (int k = 1; k < length; ++k) {
            const auto cx = static_cast<unsigned char>(utf8[i + static_cast<std::size_t>(k)]);
            if (!IsContinuation(cx)) {
                valid = false;
                break;
            }
            cp = (cp << 6U) | (cx & 0x3FU);
        }

        if (!valid || cp < min_cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            out.push_back(kReplacement);
            ++i;
            continue;
        }

        out.push_back(cp);
        i += static_cast<std::size_t>(length);
    }
    return out;
}

std::string Encode(std::u32string_view text) {
    std::string out;
    out.reserve(text.size() * 2);
    for (char32_t cp : text) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | ((cp >> 6U) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF && !(cp >= 0xD800 && cp <= 0xDFFF)) {
            out.push_back(static_cast<char>(0xE0 | ((cp >> 12U) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6U) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0x10FFFF) {
            out.push_back(static_cast<char>(0xF0 | ((cp >> 18U) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12U) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6U) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out += "\xEF\xBF\xBD";
        }
    }
    return out;
}

bool IsWhitespace(char32_t cp) {
    switch (cp) {
        case U' ': case U'\t': case U'\n': case U'\r': case U'\f': case U'\v':
        case 0x00A0: case 0x1680: case 0x2000: case 0x2001: case 0x2002:
        case 0x2003: case 0x2004: case 0x2005: case 0x2006: case 0x2007:
        case 0x2008: case 0x2009: case 0x200A: case 0x2028: case 0x2029:
        case 0x202F: case 0x205F: case 0x3000:
            return true;
        default:
            return false;
    }
}

bool IsPunctuation(char32_t cp) {
    if ((cp >= U'!' && cp <= U'/') || (cp >= U':' && cp <= U'@') ||
        (cp >= U'[' && cp <= U'`') || (cp >= U'{' && cp <= U'~')) {
        return true;
    }
    // Common CJK/full-width punctuation used in automotive corpora.
    switch (cp) {
        case U'，': case U'。': case U'、': case U'；': case U'：': case U'！': case U'？':
        case U'“': case U'”': case U'‘': case U'’': case U'（': case U'）': case U'【': case U'】':
        case U'《': case U'》': case U'—': case U'…': case U'·':
            return true;
        default:
            return false;
    }
}

std::u32string Trim(std::u32string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && IsWhitespace(text[begin])) ++begin;
    while (end > begin && IsWhitespace(text[end - 1])) --end;
    return std::u32string(text.substr(begin, end - begin));
}

} // namespace adayo::unicode
