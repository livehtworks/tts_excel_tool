#pragma once

#include "platform/UnicodePath.h"

#include <filesystem>
#include <string>
#include <string_view>

#include <wx/string.h>

namespace adayo::ui {

inline wxString WxUtf8(std::string_view utf8) {
    return wxString::FromUTF8(utf8.data(), utf8.size());
}

inline wxString WxUtf8(const char* utf8) {
    return wxString::FromUTF8(utf8);
}

inline std::string Utf8FromWx(const wxString& value) {
    const auto utf8 = value.ToUTF8();
    return utf8.data() ? std::string(utf8.data()) : std::string{};
}

inline std::filesystem::path PathFromWx(const wxString& value) {
#ifdef _WIN32
    return std::filesystem::path(value.ToStdWstring());
#else
    return PathFromUtf8(Utf8FromWx(value));
#endif
}

inline wxString WxFromPath(const std::filesystem::path& path) {
#ifdef _WIN32
    return wxString(path.native());
#else
    return WxUtf8(PathToUtf8(path));
#endif
}

} // namespace adayo::ui
