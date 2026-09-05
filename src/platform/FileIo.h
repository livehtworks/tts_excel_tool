#pragma once

#include <cstddef>
#include <filesystem>

namespace adayo {

void WriteBinaryFile(const std::filesystem::path& path, const void* data, std::size_t size);
void WriteBinaryFileAtomically(const std::filesystem::path& path, const void* data, std::size_t size);

} // namespace adayo
