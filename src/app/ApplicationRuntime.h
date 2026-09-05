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

namespace adayo {

class ApplicationRuntime {
public:
    explicit ApplicationRuntime(std::filesystem::path exe_dir);
    ~ApplicationRuntime();

    ApplicationRuntime(const ApplicationRuntime&) = delete;
    ApplicationRuntime& operator=(const ApplicationRuntime&) = delete;

    void Shutdown();

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
    void SaveConfig();

private:
    FileLogger logger_;
    JsonConfigStore config_store_;
    mutable std::mutex config_mutex_;
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
    bool shutdown_{false};
};

} // namespace adayo
