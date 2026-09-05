#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <cstdio>

namespace adayo {

// Filesystem boundary shared by production atomic writes and isolated fault tests.
class AtomicFileOperations {
public:
    virtual ~AtomicFileOperations() = default;
    virtual std::FILE* CreateExclusive(const std::filesystem::path& path);
    virtual std::size_t Write(std::FILE* file,const void* data,std::size_t size);
    virtual bool Flush(std::FILE* file);
    virtual bool Close(std::FILE* file);
    virtual void Replace(const std::filesystem::path& from,const std::filesystem::path& to);
};

void WriteBinaryFile(const std::filesystem::path& path, const void* data, std::size_t size);
void WriteBinaryFileAtomically(const std::filesystem::path& path, const void* data, std::size_t size);
void WriteBinaryFileAtomically(const std::filesystem::path& path, const void* data, std::size_t size, AtomicFileOperations& operations);
std::string Sha256(std::string_view bytes);
std::string FileSha256(const std::filesystem::path& path);
void RequireOrdinaryPath(const std::filesystem::path& path);

} // namespace adayo
