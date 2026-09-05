#include "platform/FileIo.h"

#include "platform/UnicodePath.h"

#include <chrono>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <array>
#include <random>
#include <limits>
#include <cstdio>
#include <cerrno>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <io.h>
#include <share.h>
#include <fcntl.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif

namespace adayo {
namespace {

std::filesystem::path TempSiblingPath(const std::filesystem::path& path) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto suffix = ".tmp-" + std::to_string(stamp) + "-" +
        std::to_string(std::random_device{}()) + "-" +
#ifdef _WIN32
        std::to_string(GetCurrentProcessId());
#else
        std::to_string(getpid());
#endif
    return path.parent_path() / (path.filename().wstring() + std::wstring(suffix.begin(), suffix.end()));
}

void ReplaceFileAtomically(const std::filesystem::path& from, const std::filesystem::path& to) {
#ifdef _WIN32
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::runtime_error("替换文件失败: " + PathToUtf8(to));
    }
#else
    std::filesystem::rename(from, to);
#endif
}

} // namespace

void WriteBinaryFile(const std::filesystem::path& path, const void* data, std::size_t size) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("无法写入文件: " + PathToUtf8(path));
    }
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!out) {
        throw std::runtime_error("写入文件失败: " + PathToUtf8(path));
    }
    out.flush();
    if (!out) throw std::runtime_error("File flush failed: " + PathToUtf8(path));
    out.close();
    if (!out) throw std::runtime_error("File close failed: " + PathToUtf8(path));
}

void WriteBinaryFileAtomically(const std::filesystem::path& path, const void* data, std::size_t size) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    const auto temp = TempSiblingPath(path);
    try {
        // Exclusive creation prevents a competing writer from owning our temporary file.
#ifdef _WIN32
        int fd = -1;
        if (_wsopen_s(&fd, temp.c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY,
            _SH_DENYRW, _S_IREAD | _S_IWRITE) != 0) throw std::runtime_error("Exclusive temporary creation failed");
        auto* file = _fdopen(fd, "wb");
        if (!file) { _close(fd); throw std::runtime_error("Temporary stream open failed"); }
#else
        const int fd = open(temp.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (fd < 0) throw std::runtime_error("Exclusive temporary creation failed");
        auto* file = fdopen(fd, "wb");
        if (!file) { close(fd); throw std::runtime_error("Temporary stream open failed"); }
#endif
        const bool written = size == 0 || std::fwrite(data, 1, size, file) == size;
        const bool flushed = std::fflush(file) == 0;
        const bool closed = std::fclose(file) == 0;
        if (!written || !flushed || !closed) throw std::runtime_error("Temporary write/flush/close failed");
        ReplaceFileAtomically(temp, path);
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        throw;
    }
}

void RequireOrdinaryPath(const std::filesystem::path& path) {
    for (auto current = std::filesystem::absolute(path).lexically_normal(); !current.empty();) {
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Reparse path rejected: " + PathToUtf8(current));
#else
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(current)))
            throw std::runtime_error("Symlink path rejected");
#endif
        const auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
}

namespace {
#ifdef _WIN32
std::string HashInput(std::istream* stream, std::string_view bytes) {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("SHA256 provider failed");
    try {
        if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) throw std::runtime_error("SHA256 creation failed");
        auto append = [&](const char* data, std::size_t count) {
            while (count) {
                const auto chunk = static_cast<ULONG>((std::min)(count, std::size_t{1048576}));
                if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data)), chunk, 0) < 0)
                    throw std::runtime_error("SHA256 update failed");
                data += chunk; count -= chunk;
            }
        };
        if (stream) {
            std::array<char, 65536> buffer{};
            while (stream->read(buffer.data(), buffer.size()) || stream->gcount()) append(buffer.data(), static_cast<std::size_t>(stream->gcount()));
            if (!stream->eof()) throw std::runtime_error("SHA256 input read failed");
        } else append(bytes.data(), bytes.size());
        std::array<unsigned char, 32> digest{};
        if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) throw std::runtime_error("SHA256 finish failed");
        BCryptDestroyHash(hash); hash = nullptr;
        BCryptCloseAlgorithmProvider(algorithm, 0); algorithm = nullptr;
        constexpr char hex[] = "0123456789abcdef";
        std::string output;
        for (auto byte : digest) { output += hex[byte >> 4]; output += hex[byte & 15]; }
        return output;
    } catch (...) {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        throw;
    }
}
#endif
}
std::string Sha256(std::string_view bytes) {
#ifdef _WIN32
    return HashInput(nullptr, bytes);
#else
    throw std::runtime_error("SHA256 platform provider unsupported in this build");
#endif
}
std::string FileSha256(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot hash file: " + PathToUtf8(path));
#ifdef _WIN32
    return HashInput(&stream, {});
#else
    throw std::runtime_error("SHA256 platform provider unsupported in this build");
#endif
}

} // namespace adayo
