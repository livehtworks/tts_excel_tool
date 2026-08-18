#include "adapters/text/TextFileImporter.h"

#include <fstream>
#include <stdexcept>

namespace adayo {
namespace {
std::string StripUtf8Bom(std::string text) {
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    return text;
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

void AddRecord(std::vector<std::string>& records, std::string record, bool skip_empty) {
    TrimLineBoundary(record);
    if (skip_empty && record.empty()) return;
    records.push_back(std::move(record));
}
} // namespace

std::vector<std::string> TextFileImporter::ReadUtf8Records(const std::filesystem::path& path, const TextImportOptions& options) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("无法读取文本文件: " + path.string());
    }
    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return SplitUtf8Records(content, options);
}

std::vector<std::string> TextFileImporter::SplitUtf8Records(std::string_view content, const TextImportOptions& options) {
    const std::string text = StripUtf8Bom(std::string(content));
    std::vector<std::string> records;
    if (options.delimiter.empty()) {
        std::size_t begin = 0;
        while (begin <= text.size()) {
            const auto end = text.find('\n', begin);
            AddRecord(records,
                end == std::string::npos ? text.substr(begin) : text.substr(begin, end - begin),
                options.skip_empty);
            if (end == std::string::npos) break;
            begin = end + 1;
        }
        return records;
    }

    std::size_t begin = 0;
    while (begin <= text.size()) {
        const auto end = text.find(options.delimiter, begin);
        AddRecord(records,
            end == std::string::npos ? text.substr(begin) : text.substr(begin, end - begin),
            options.skip_empty);
        if (end == std::string::npos) break;
        begin = end + options.delimiter.size();
    }
    return records;
}

} // namespace adayo
