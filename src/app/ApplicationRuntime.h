#pragma once

#include "adapters/audio/MiniaudioPlayer.h"
#include "core/worker/WorkerQueue.h"
#include "persistence/JsonConfigStore.h"
#include "services/FileLogger.h"
#include "services/ModelRegistry.h"
#include "services/PlaybackService.h"
#include "services/TtsService.h"
#include "services/WorkbookService.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>

namespace adayo {

class ApplicationRuntime {
public:
    explicit ApplicationRuntime(std::filesystem::path exe_dir, AtomicFileOperations* config_io = nullptr);
    ~ApplicationRuntime();

    ApplicationRuntime(const ApplicationRuntime&) = delete;
    ApplicationRuntime& operator=(const ApplicationRuntime&) = delete;

    void Shutdown();
    void RequestShutdown();
    bool ShutdownComplete() const noexcept { return shutdown_complete_.load(); }
    std::string ShutdownStatus() const;

    WorkerQueue& BackgroundJobs() noexcept { return background_worker_; }
    WorkbookService& Workbook() noexcept { return *workbook_service_; }
    TtsService& Tts() noexcept { return tts_service_; }
    PlaybackService& Playback() noexcept { return playback_service_; }
    FileLogger& Logger() noexcept { return logger_; }
    const ModelRegistryScanResult& ModelScan() const noexcept { return model_scan_; }
    AppConfig ConfigSnapshot() const;
    ConfigLoadStatus ConfigStatus() const noexcept { return config_load_status_; }
    const std::string& ConfigLoadMessage() const noexcept { return config_load_message_; }
    bool ConfigSaveAllowed() const noexcept { return config_save_allowed_; }
    void UpdateConfig(const std::function<void(AppConfig&)>& update);
    void SaveConfig(const std::function<void(AppConfig&)>& update = {},
        const std::function<void(AppConfig&, const AppConfig&)>& rollback = {});

private:
    FileLogger logger_;
    JsonConfigStore config_store_;
    mutable std::mutex config_mutex_;
    std::mutex config_save_mutex_;
    AppConfig config_;
    ConfigLoadStatus config_load_status_{ConfigLoadStatus::Missing};
    std::string config_load_message_;
    bool config_save_allowed_{true};
    ModelRegistry model_registry_;
    std::unique_ptr<WorkbookService> workbook_service_;
    TtsService tts_service_;
    MiniaudioPlayer audio_player_;
    PlaybackService playback_service_;
    WorkerQueue background_worker_;
    ModelRegistryScanResult model_scan_;
    mutable std::mutex shutdown_mutex_;
    std::mutex shutdown_join_mutex_;
    bool shutdown_started_{false};
    std::atomic<bool> shutdown_complete_{false};
    std::string shutdown_status_;
    std::jthread shutdown_worker_;
};

} // namespace adayo
