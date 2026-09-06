#include "ui/MainFrame.h"

#include "ui/UiString.h"
#include "services/FileLogger.h"

#include <wx/app.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>
#include <wx/msw/wrapwin.h>
#include <commctrl.h>

#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

class AdayoApp final : public wxApp {
public:
    bool OnInit() override {
        try {
            log_dir_ = adayo::ui::PathFromWx(wxStandardPaths::Get().GetExecutablePath()).parent_path() / "logs";
            auto frame = std::make_unique<adayo::ui::MainFrame>();
            if (!SetWindowSubclass(static_cast<HWND>(frame->GetHandle()), ObserveNativeDestruction,
                    1, reinterpret_cast<DWORD_PTR>(this))) {
                throw std::runtime_error("Cannot register native window lifecycle observer");
            }
            frame->Show(true);
            frame.release();
            return true;
        } catch (const std::exception& ex) {
            wxMessageBox(adayo::ui::WxUtf8(std::string("启动失败：") + ex.what()),
                adayo::ui::WxUtf8("Adayo语料测试"),
                wxOK | wxICON_ERROR);
            return false;
        } catch (...) {
            wxMessageBox(adayo::ui::WxUtf8("启动失败：未知错误"),
                adayo::ui::WxUtf8("Adayo语料测试"),
                wxOK | wxICON_ERROR);
            return false;
        }
    }

    int OnExit() override {
        const auto result = wxApp::OnExit();
        try {
            // Runtime logging has ended; persist the native event time, not this later write time.
            adayo::FileLogger logger(log_dir_);
            if (!window_destroyed_ns_) {
                logger.Error("lifecycle", "window_destroyed NOT_OBSERVED");
                return 1;
            }
            logger.Info("lifecycle", "window_destroyed steady_ns=" + std::to_string(*window_destroyed_ns_) +
                ",source=WM_NCDESTROY_after_default");
        } catch (const std::exception& ex) {
            wxLogError("%s", adayo::ui::WxUtf8(std::string("Lifecycle log failed: ") + ex.what()));
            return 1;
        }
        return result;
    }

private:
    static LRESULT CALLBACK ObserveNativeDestruction(HWND window, UINT message, WPARAM wparam,
        LPARAM lparam, UINT_PTR id, DWORD_PTR data) {
        if (message != WM_NCDESTROY) return DefSubclassProc(window, message, wparam, lparam);
        RemoveWindowSubclass(window, ObserveNativeDestruction, id);
        const auto result = DefSubclassProc(window, message, wparam, lparam);
        auto* app = reinterpret_cast<AdayoApp*>(data);
        app->window_destroyed_ns_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        return result;
    }

    std::filesystem::path log_dir_;
    std::optional<std::int64_t> window_destroyed_ns_;
};

wxIMPLEMENT_APP(AdayoApp);
