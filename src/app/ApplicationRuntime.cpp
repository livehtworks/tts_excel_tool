#include "app/ApplicationRuntime.h"

#include "adapters/excel/OpenXlsxWorkbookReader.h"
#include "adapters/tts/SherpaOnnxTtsEngine.h"

#include <memory>
#include <stdexcept>

namespace adayo {

ApplicationRuntime::ApplicationRuntime(std::filesystem::path exe_dir, AtomicFileOperations* config_io)
    : logger_(exe_dir / "logs"),
      config_store_(exe_dir / "config" / "config.json",config_io),
      model_registry_(exe_dir / "model" / "sherpa"),
      workbook_service_(std::make_unique<WorkbookService>(std::make_unique<OpenXlsxWorkbookReader>())),
      playback_service_(tts_service_, audio_player_, [this](const std::string& error) {
          logger_.Error("playback", error);
      }, [this](std::uint64_t request,std::size_t item,const TtsTimings& t,double device,
                std::optional<double> first,std::optional<double> done,bool canceled) {
          logger_.Info("audio-timing","request_id="+std::to_string(request)+",item="+std::to_string(item)+
              ",source="+t.source+",key_build_ms="+std::to_string(t.key_build_ms)+",model_validation_ms="+std::to_string(t.model_validation_ms)+
              ",lookup_ms="+std::to_string(t.lookup_ms)+",model_load_ms="+std::to_string(t.model_load_ms)+
              ",synth_ms="+std::to_string(t.synth_ms)+",cache_read_ms="+std::to_string(t.cache_read_ms)+
              ",cache_write_ms="+std::to_string(t.cache_write_ms)+",audio_prepare_ms="+std::to_string(t.audio_prepare_ms)+
              ",device_init_ms="+std::to_string(device)+",first_nonzero_callback_ms="+(first?std::to_string(*first):"NOT_RUN")+
              ",playback_done_ms="+(done?std::to_string(*done):"NOT_RUN")+",load_call_delta="+std::to_string(t.load_call_delta)+
              ",synth_call_delta="+std::to_string(t.synth_call_delta)+",canceled="+(canceled?"true":"false")+
              ",logical_model_id="+t.logical_model_id+",model_load_identity="+t.model_load_identity);
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
    tts_service_.SetNativeReturnHandler([this](std::string_view operation) {
        logger_.Info("lifecycle","native_returned operation="+std::string(operation)+",steady_ns="+std::to_string(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()));
    });
    tts_service_.InitializeCache(exe_dir / "cache" / "tts-v1", config_.audio_cache);
    if (!tts_service_.Cache()->Stats().warning.empty()) logger_.Warn("cache", tts_service_.Cache()->Stats().warning);
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
    RequestShutdown();
    std::lock_guard join_lock(shutdown_join_mutex_);
    if(shutdown_worker_.get_id()==std::this_thread::get_id())
        throw std::logic_error("Runtime cannot join its shutdown task");
    if(shutdown_worker_.joinable()) shutdown_worker_.join();
}

std::string ApplicationRuntime::ShutdownStatus() const {
    std::lock_guard lock(shutdown_mutex_);
    return shutdown_status_;
}

void ApplicationRuntime::RequestShutdown() {
    std::lock_guard lock(shutdown_mutex_);
    if(shutdown_started_) return;
    shutdown_status_="正在停止，等待当前原生操作返回";
    shutdown_worker_=std::jthread([this] {
        // Do not join until the caller has published cancellation under this mutex.
        { std::lock_guard start_lock(shutdown_mutex_); }
        try {
            background_worker_.Stop(StopMode::DiscardPending);
            playback_service_.Shutdown();
            logger_.Info("lifecycle","worker_joined steady_ns="+std::to_string(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()));
            audio_player_.Stop();
            if(tts_service_.Cache()) tts_service_.Cache()->FlushUsage();
            tts_service_.Unload();
            logger_.Info("app","shutdown complete");
            logger_.Close();
            { std::lock_guard status_lock(shutdown_mutex_); shutdown_status_="停止完成"; }
            shutdown_complete_.store(true);
        } catch(const std::exception& ex) {
            logger_.Error("lifecycle",ex.what());
            std::lock_guard status_lock(shutdown_mutex_);
            shutdown_status_="停止未完成："+std::string(ex.what());
        }
    });
    shutdown_started_=true;
    logger_.Info("lifecycle","cancel_requested steady_ns="+std::to_string(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()));
    background_worker_.RequestStop(StopMode::DiscardPending);
    playback_service_.RequestShutdown();
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

void ApplicationRuntime::SaveConfig(const std::function<void(AppConfig&)>& update,
    const std::function<void(AppConfig&, const AppConfig&)>& rollback) {
    // Order is save mutex -> snapshot mutex; disk IO never holds config_mutex_.
    std::lock_guard save_lock(config_save_mutex_);
    AppConfig snapshot, previous;
    {
        std::lock_guard lock(config_mutex_);
        snapshot = config_;
        if(update) {
            if(!config_save_allowed_) throw std::runtime_error("当前配置不可覆盖保存: "+config_load_message_);
            if(!rollback) throw std::invalid_argument("A persisted field update requires field-scoped rollback");
            previous=snapshot;
            update(snapshot);
            config_=snapshot;
        }
    }
    try { config_store_.Save(snapshot); }
    catch(...) {
        if(update) { std::lock_guard lock(config_mutex_); rollback(config_,previous); }
        throw;
    }
}

} // namespace adayo
