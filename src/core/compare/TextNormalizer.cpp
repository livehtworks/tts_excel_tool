#include "core/compare/TextNormalizer.h"

#include "core/unicode/Utf8.h"

#include <cstdlib>
#include <stdexcept>

#ifdef ADAYO_HAS_UTF8PROC
#include <utf8proc.h>
#endif

namespace adayo {
namespace {
std::string ApplyUnicodeMap(std::string_view raw, const NormalizerOptions& options) {
    if(options.normalization==UnicodeNormalization::None && !options.case_fold) return std::string(raw);
#ifndef ADAYO_HAS_UTF8PROC
    throw std::runtime_error("UNSUPPORTED_UNICODE: normalization/casefold requires utf8proc");
#endif
#ifdef ADAYO_HAS_UTF8PROC
    std::string composed;
    if(options.case_fold && options.normalization==UnicodeNormalization::Nfc) {
        auto before=options; before.case_fold=false;
        composed=ApplyUnicodeMap(raw,before); raw=composed;
    }
    utf8proc_uint8_t* mapped = nullptr;
    utf8proc_option_t flags = UTF8PROC_STABLE;
    if(options.normalization!=UnicodeNormalization::None) flags=static_cast<utf8proc_option_t>(flags|UTF8PROC_COMPOSE);
    if (options.normalization==UnicodeNormalization::Nfkc) {
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
    throw std::runtime_error("Unicode normalization failed");
#endif
}
}

TextNormalizer::TextNormalizer(NormalizerOptions options) : options_(options) {}

std::string TextNormalizer::Normalize(std::string_view raw) const {
    return unicode::Encode(NormalizeCodepoints(unicode::DecodeStrict(raw)));
}
std::u32string TextNormalizer::NormalizeCodepoints(std::u32string_view raw) const {
    auto cps=options_.normalization==UnicodeNormalization::None && !options_.case_fold?
        std::u32string(raw):unicode::DecodeStrict(ApplyUnicodeMap(unicode::Encode(raw),options_));
    for(auto cp:cps) if(cp==0 || (cp>=0xd800 && cp<=0xdfff) || cp>0x10ffff) throw std::invalid_argument("Invalid Unicode code point");

    std::u32string out;
    out.reserve(cps.size());
    bool last_was_space = false;

    for (char32_t cp : cps) {
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
    return out;
}

} // namespace adayo
