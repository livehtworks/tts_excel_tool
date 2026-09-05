#include "services/FileLogger.h"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace adayo {
namespace {

std::tm LocalTime(std::time_t value) {
    std::tm result{};
#ifdef _WIN32
    localtime_s(&result, &value);
#else
    localtime_r(&value, &result);
#endif
    return result;
}

std::string Timestamp(bool date_only = false) {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    const auto local = LocalTime(time);
    std::ostringstream out;
    if (date_only) {
        out << std::put_time(&local, "%Y%m%d");
        return out.str();
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    out << std::put_time(&local, "%Y-%m-%d %H:%M:%S")
        << "." << std::setw(3) << std::setfill('0') << ms;
    return out.str();
}

} // namespace

FileLogger::FileLogger(std::filesystem::path log_dir) {
    std::filesystem::create_directories(log_dir);
    output_.open(log_dir / ("AdayoCorpusTool_" + Timestamp(true) + ".log"), std::ios::binary | std::ios::app);
    if (!output_.is_open()) {
        throw std::runtime_error("无法打开日志文件目录");
    }
    Write("INFO", "app", "logger started");
}

FileLogger::~FileLogger() {
    Close();
}

void FileLogger::Info(const std::string& module, const std::string& message) {
    Write("INFO", module, message);
}

void FileLogger::Warn(const std::string& module, const std::string& message) {
    Write("WARN", module, message);
}

void FileLogger::Error(const std::string& module, const std::string& message) {
    Write("ERROR", module, message);
}

void FileLogger::Close() {
    std::lock_guard lock(mutex_);
    if (closed_) return;
    if (output_.is_open()) {
        output_ << Timestamp() << " | INFO | app | logger closed\n";
        output_.flush();
        output_.close();
    }
    closed_ = true;
}

void FileLogger::Write(const char* level, const std::string& module, const std::string& message) {
    std::lock_guard lock(mutex_);
    if (closed_ || !output_.is_open()) return;
    output_ << Timestamp() << " | " << level << " | " << module << " | " << message << "\n";
    output_.flush();
}

} // namespace adayo
