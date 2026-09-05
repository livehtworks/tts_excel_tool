#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace adayo {

class FileLogger {
public:
    explicit FileLogger(std::filesystem::path log_dir);
    ~FileLogger();

    FileLogger(const FileLogger&) = delete;
    FileLogger& operator=(const FileLogger&) = delete;

    void Info(const std::string& module, const std::string& message);
    void Warn(const std::string& module, const std::string& message);
    void Error(const std::string& module, const std::string& message);
    void Close();

private:
    void Write(const char* level, const std::string& module, const std::string& message);

    std::mutex mutex_;
    std::ofstream output_;
    bool closed_{false};
};

} // namespace adayo
