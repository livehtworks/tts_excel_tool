#include "core/compare/TextNormalizer.h"

#include "core/unicode/Utf8.h"

#include <cstdlib>

#ifdef ADAYO_HAS_UTF8PROC
#include <utf8proc.h>
#endif

namespace adayo {
namespace {
std::string ApplyUnicodeMap(std::string_view raw, const NormalizerOptions& options) {
#ifndef ADAYO_HAS_UTF8PROC
    (void)options;
#endif
#ifdef ADAYO_HAS_UTF8PROC
    utf8proc_uint8_t* mapped = nullptr;
    utf8proc_option_t flags = static_cast<utf8proc_option_t>(UTF8PROC_STABLE | UTF8PROC_COMPOSE);
    if (options.unicode_nfkc) {
        flags = static_cast<utf8proc_option_t>(flags | UTF8PROC_COMPAT);
    }
    if (options.case_fold) {
        flags = static_cast<utf8proc_option_t>(flags | UTF8PROC_CASEFOLD);
    }
    const auto rc = utf8proc_map(
        reinterpret_cast<const utf8proc_uint8_t*>(raw.data()),
        static_cast<utf8proc_ssize_t>(raw.size()),
        &mapped,
        flags);
    if (rc >= 0 && mapped) {
        std::string result(reinterpret_cast<char*>(mapped), static_cast<std::size_t>(rc));
        std::free(mapped);
        return result;
    }
    if (mapped) std::free(mapped);
#endif
    return std::string(raw);
}
}

TextNormalizer::TextNormalizer(NormalizerOptions options) : options_(options) {}

std::string TextNormalizer::Normalize(std::string_view raw) const {
    const std::string unicode_mapped = ApplyUnicodeMap(raw, options_);
    auto cps = unicode::Decode(unicode_mapped);

    std::u32string out;
    out.reserve(cps.size());
    bool last_was_space = false;

    for (char32_t cp : cps) {
#ifndef ADAYO_HAS_UTF8PROC
        // Fallback keeps Unicode code points intact. Full NFKC/casefold is a production dependency task.
        if (options_.case_fold && cp >= U'A' && cp <= U'Z') {
            cp = static_cast<char32_t>(cp - U'A' + U'a');
        }
#endif
        if (options_.ignore_punctuation && unicode::IsPunctuation(cp)) {
            continue;
        }
        if (options_.collapse_whitespace && unicode::IsWhitespace(cp)) {
            if (!last_was_space) out.push_back(U' ');
            last_was_space = true;
            continue;
        }
        last_was_space = false;
        out.push_back(cp);
    }

    if (options_.trim) {
        out = unicode::Trim(out);
    }
    return unicode::Encode(out);
}

} // namespace adayo
