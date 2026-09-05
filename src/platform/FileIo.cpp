#include "platform/FileIo.h"

#include "platform/UnicodePath.h"

#include <chrono>
#include <fstream>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace adayo {
namespace {

std::filesystem::path TempSiblingPath(const std::filesystem::path& path) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto suffix = ".tmp-" + std::to_string(stamp) + "-" +
        std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
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
}

void WriteBinaryFileAtomically(const std::filesystem::path& path, const void* data, std::size_t size) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    const auto temp = TempSiblingPath(path);
    try {
        WriteBinaryFile(temp, data, size);
        ReplaceFileAtomically(temp, path);
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        throw;
    }
}

} // namespace adayo
