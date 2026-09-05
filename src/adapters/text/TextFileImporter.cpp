#include "adapters/text/TextFileImporter.h"

#include "core/unicode/Utf8.h"
#include "platform/UnicodePath.h"
#include "core/compare/CompareExecutionContext.h"

#include <fstream>
#include <stdexcept>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace adayo {
namespace {
std::string DecodeUtf16(std::string_view bytes, bool little_endian) {
    if (bytes.size() % 2 != 0) {
        throw std::runtime_error("UTF-16 文本字节数不是偶数");
    }
    std::u32string out;
    out.reserve(bytes.size() / 2);
    auto read_unit = [&](std::size_t offset) {
        const auto b0 = static_cast<unsigned char>(bytes[offset]);
        const auto b1 = static_cast<unsigned char>(bytes[offset + 1]);
        return static_cast<char16_t>(little_endian ? (b0 | (b1 << 8U)) : ((b0 << 8U) | b1));
    };
    for (std::size_t i = 0; i < bytes.size(); i += 2) {
        const char16_t unit = read_unit(i);
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            if (i + 3 >= bytes.size()) {
                throw std::runtime_error("UTF-16 高代理项缺少低代理项");
            }
            const char16_t low = read_unit(i + 2);
            if (low < 0xDC00 || low > 0xDFFF) {
                throw std::runtime_error("UTF-16 代理项不合法");
            }
            const char32_t cp = 0x10000 +
                ((static_cast<char32_t>(unit - 0xD800) << 10U) | static_cast<char32_t>(low - 0xDC00));
            out.push_back(cp);
            i += 2;
        } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
            throw std::runtime_error("UTF-16 低代理项缺少高代理项");
        } else {
            out.push_back(static_cast<char32_t>(unit));
        }
    }
    return unicode::Encode(out);
}

bool IsStrictUtf8(std::string_view utf8) {
    std::size_t i = 0;
    while (i < utf8.size()) {
        const auto c0 = static_cast<unsigned char>(utf8[i]);
        if (c0 < 0x80U) {
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
            return false;
        }
        if (i + static_cast<std::size_t>(length) > utf8.size()) return false;
        for (int k = 1; k < length; ++k) {
            const auto cx = static_cast<unsigned char>(utf8[i + static_cast<std::size_t>(k)]);
            if ((cx & 0xC0U) != 0x80U) return false;
            cp = (cp << 6U) | (cx & 0x3FU);
        }
        if (cp < min_cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        i += static_cast<std::size_t>(length);
    }
    return true;
}

std::string DecodeGb18030(std::string_view bytes) {
#ifdef _WIN32
    if (bytes.empty()) return {};
    constexpr UINT kGb18030 = 54936;
    const int wide_len = MultiByteToWideChar(
        kGb18030,
        MB_ERR_INVALID_CHARS,
        bytes.data(),
        static_cast<int>(bytes.size()),
        nullptr,
        0);
    if (wide_len <= 0) {
        throw std::runtime_error("文本既不是严格 UTF-8，也无法按 GB18030 解码");
    }
    std::wstring wide(static_cast<std::size_t>(wide_len), L'\0');
    const int converted = MultiByteToWideChar(
        kGb18030,
        MB_ERR_INVALID_CHARS,
        bytes.data(),
        static_cast<int>(bytes.size()),
        wide.data(),
        wide_len);
    if (converted != wide_len) {
        throw std::runtime_error("GB18030 文本解码失败");
    }
    const int utf8_len = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        wide.data(),
        wide_len,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (utf8_len <= 0) {
        throw std::runtime_error("GB18030 文本转换 UTF-8 失败");
    }
    std::string utf8(static_cast<std::size_t>(utf8_len), '\0');
    const int encoded = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        wide.data(),
        wide_len,
        utf8.data(),
        utf8_len,
        nullptr,
        nullptr);
    if (encoded != utf8_len) {
        throw std::runtime_error("GB18030 文本转换 UTF-8 失败");
    }
    return utf8;
#else
    (void)bytes;
    throw std::runtime_error("当前平台不支持 GB18030 自动解码");
#endif
}

std::string DecodeText(std::string_view content, TextEncoding encoding) {
    TextEncoding bom=TextEncoding::Auto;
    std::size_t prefix=0;
    if(content.starts_with("\xEF\xBB\xBF")) {bom=TextEncoding::Utf8;prefix=3;}
    else if(content.starts_with("\xFF\xFE")) {bom=TextEncoding::Utf16LE;prefix=2;}
    else if(content.starts_with("\xFE\xFF")) {bom=TextEncoding::Utf16BE;prefix=2;}
    if(prefix) {
        if(encoding!=TextEncoding::Auto && encoding!=bom) throw std::runtime_error("Explicit text encoding conflicts with BOM");
        encoding=bom; content.remove_prefix(prefix);
    }
    if (encoding == TextEncoding::Utf8) {
        (void)unicode::DecodeStrict(content);
        return std::string(content);
    }
    if (encoding == TextEncoding::Utf16LE) return DecodeUtf16(content, true);
    if (encoding == TextEncoding::Utf16BE) return DecodeUtf16(content, false);
    if (encoding == TextEncoding::Gb18030) return DecodeGb18030(content);

    if (IsStrictUtf8(content)) return std::string(content);
    return DecodeGb18030(content);
}

void TrimLineBoundary(std::string& text) {
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n')) {
        text.pop_back();
    }
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == '\r' || text[begin] == '\n')) {
        ++begin;
    }
    if (begin > 0) text.erase(0, begin);
}

void AddRecord(std::vector<std::string>& records, std::string record, bool skip_empty, bool legacy) {
    if(legacy) TrimLineBoundary(record);
    if(unicode::DecodeStrict(record).size()>CompareExecutionContext::record_codepoints) throw std::runtime_error("Text record exceeds 65536 codepoints");
    if (skip_empty && unicode::Trim(unicode::Decode(record)).empty()) return;
    records.push_back(std::move(record));
}
} // namespace

std::vector<std::string> TextFileImporter::ReadUtf8Records(const std::filesystem::path& path, const TextImportOptions& options) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("无法读取文本文件: " + PathToUtf8(path));
    }
    const auto bytes=std::filesystem::file_size(path);
    if(bytes>CompareExecutionContext::file_bytes) throw std::runtime_error("Text file exceeds 64MiB input budget");
    std::string content(static_cast<std::size_t>(bytes),'\0');
    input.read(content.data(),static_cast<std::streamsize>(content.size()));
    if(!input || input.peek()!=std::char_traits<char>::eof()) throw std::runtime_error("Text input changed or could not be read completely");
    return SplitUtf8Records(content, options);
}

std::vector<std::string> TextFileImporter::SplitUtf8Records(std::string_view content, const TextImportOptions& options) {
    if(content.size()>CompareExecutionContext::file_bytes) throw std::runtime_error("Text input exceeds 64MiB input budget");
    const std::string text = DecodeText(content, options.encoding);
    (void)unicode::DecodeStrict(text);
    (void)unicode::DecodeStrict(options.delimiter);
    const bool legacy=options.empty_records==EmptyRecordPolicy::Legacy;
    const bool skip=legacy && options.skip_empty;
    std::vector<std::string> records;
    if (options.delimiter.empty()) {
        std::size_t begin = 0;
        while (begin < text.size()) {
            const auto end = text.find('\n', begin);
            auto record=end == std::string::npos ? text.substr(begin) : text.substr(begin, end - begin);
            if(end!=std::string::npos && !record.empty() && record.back()=='\r') record.pop_back();
            AddRecord(records,std::move(record),skip,legacy);
            if (end == std::string::npos) break;
            begin = end + 1;
        }
        return records;
    }

    std::size_t begin = 0;
    while (begin < text.size()) {
        const auto end = text.find(options.delimiter, begin);
        AddRecord(records,
            end == std::string::npos ? text.substr(begin) : text.substr(begin, end - begin),
            skip,legacy);
        if (end == std::string::npos) break;
        begin = end + options.delimiter.size();
    }
    return records;
}

} // namespace adayo
