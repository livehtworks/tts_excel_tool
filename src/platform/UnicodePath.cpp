#include "platform/UnicodePath.h"

#include <stdexcept>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace adayo {
namespace {
#ifdef _WIN32
std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int wide_size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        utf8.data(),
        static_cast<int>(utf8.size()),
        nullptr,
        0);
    if (wide_size <= 0) {
        throw std::runtime_error("UTF-8 path decode failed");
    }
    std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
    const int converted = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        utf8.data(),
        static_cast<int>(utf8.size()),
        wide.data(),
        wide_size);
    if (converted != wide_size) {
        throw std::runtime_error("UTF-8 path decode size mismatch");
    }
    return wide;
}

std::string WideToUtf8(std::wstring_view wide) {
    if (wide.empty()) return {};
    const int utf8_size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        wide.data(),
        static_cast<int>(wide.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (utf8_size <= 0) {
        throw std::runtime_error("Windows path UTF-8 encode failed");
    }
    std::string utf8(static_cast<std::size_t>(utf8_size), '\0');
    const int converted = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        wide.data(),
        static_cast<int>(wide.size()),
        utf8.data(),
        utf8_size,
        nullptr,
        nullptr);
    if (converted != utf8_size) {
        throw std::runtime_error("Windows path UTF-8 encode size mismatch");
    }
    return utf8;
}
#endif
} // namespace

std::string PathToUtf8(const std::filesystem::path& path) {
#ifdef _WIN32
    return WideToUtf8(path.native());
#else
    const auto text = path.u8string();
    return {text.begin(), text.end()};
#endif
}

std::filesystem::path PathFromUtf8(std::string_view utf8) {
#ifdef _WIN32
    return std::filesystem::path(Utf8ToWide(utf8));
#else
    return std::filesystem::path(std::string(utf8));
#endif
}

} // namespace adayo
