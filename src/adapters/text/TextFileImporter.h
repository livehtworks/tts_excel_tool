#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace adayo {

enum class TextEncoding {
    Auto,
    Utf8,
    Utf16LE,
    Utf16BE,
    Gb18030,
};

struct TextImportOptions {
    std::string delimiter;
    bool skip_empty{true};
    TextEncoding encoding{TextEncoding::Auto};
};

class TextFileImporter {
public:
    static std::vector<std::string> ReadUtf8Records(const std::filesystem::path& path, const TextImportOptions& options = {});
    static std::vector<std::string> SplitUtf8Records(std::string_view content, const TextImportOptions& options = {});
};

} // namespace adayo
