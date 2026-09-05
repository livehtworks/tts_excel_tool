#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace adayo {

void WriteBinaryFile(const std::filesystem::path& path, const void* data, std::size_t size);
void WriteBinaryFileAtomically(const std::filesystem::path& path, const void* data, std::size_t size);
std::string Sha256(std::string_view bytes);
std::string FileSha256(const std::filesystem::path& path);
void RequireOrdinaryPath(const std::filesystem::path& path);

} // namespace adayo
