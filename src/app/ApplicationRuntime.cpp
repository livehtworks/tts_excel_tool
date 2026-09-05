#include "app/ApplicationRuntime.h"

#include "adapters/excel/OpenXlsxWorkbookReader.h"
#include "adapters/tts/SherpaOnnxTtsEngine.h"

#include <memory>
#include <stdexcept>

namespace adayo {

ApplicationRuntime::ApplicationRuntime(std::filesystem::path exe_dir)
    : logger_(exe_dir / "logs"),
      config_store_(exe_dir / "config" / "config.json"),
      model_registry_(exe_dir / "model" / "sherpa"),
      workbook_service_(std::make_unique<WorkbookService>(std::make_unique<OpenXlsxWorkbookReader>())),
      playback_service_(tts_service_, audio_player_, [this](const std::string& error) {
          logger_.Error("playback", error);
      }),
      background_worker_([this](std::exception_ptr error) {
          try {
              if (error) std::rethrow_exception(error);
          } catch (const std::exception& ex) {
              logger_.Error("worker", ex.what());
          } catch (...) {
              logger_.Error("worker", "未知后台任务错误");
          }
      }) {
    logger_.Info("app", "app start");
    const auto config_load = config_store_.LoadOrDefault();
    config_ = config_load.config;
    config_load_status_ = config_load.status;
    config_load_message_ = config_load.message;
    config_save_allowed_ = config_load.allow_save;
    if (!config_load_message_.empty()) {
        logger_.Warn("config", config_load_message_);
    }
    tts_service_.SetEngine(std::make_unique<SherpaOnnxTtsEngine>());
    model_scan_ = model_registry_.ScanSherpaModelsWithDiagnostics();
    logger_.Info("model", "valid models=" + std::to_string(model_scan_.entries.size()) +
        ", invalid models=" + std::to_string(model_scan_.invalid.size()));
    for (const auto& diagnostic : model_scan_.invalid) {
        logger_.Warn("model", diagnostic.id + ": " + diagnostic.error);
    }
}

ApplicationRuntime::~ApplicationRuntime() {
    Shutdown();
}

void ApplicationRuntime::Shutdown() {
    if (shutdown_) return;
    shutdown_ = true;
    logger_.Info("app", "shutdown begin");
    background_worker_.Stop(StopMode::DiscardPending);
    playback_service_.Shutdown();
    audio_player_.Stop();
    tts_service_.Unload();
    logger_.Info("app", "shutdown complete");
    logger_.Close();
}

AppConfig ApplicationRuntime::ConfigSnapshot() const {
    std::lock_guard lock(config_mutex_);
    return config_;
}

void ApplicationRuntime::UpdateConfig(const std::function<void(AppConfig&)>& update) {
    if (!config_save_allowed_) {
        throw std::runtime_error("当前配置不可覆盖保存: " + config_load_message_);
    }
    std::lock_guard lock(config_mutex_);
    update(config_);
}

void ApplicationRuntime::SaveConfig() {
    AppConfig snapshot;
    {
        std::lock_guard lock(config_mutex_);
        snapshot = config_;
    }
    config_store_.Save(snapshot);
}

} // namespace adayo
