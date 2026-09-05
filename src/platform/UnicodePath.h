#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace adayo {

std::string PathToUtf8(const std::filesystem::path& path);
std::filesystem::path PathFromUtf8(std::string_view utf8);

} // namespace adayo
