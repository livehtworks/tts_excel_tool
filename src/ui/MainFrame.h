#pragma once

#include <memory>

#include <wx/frame.h>
#include <wx/timer.h>

namespace adayo {
class ApplicationRuntime;
}

namespace adayo::ui {
class TtsPanel;
class ComparePanel;

class MainFrame final : public wxFrame {
public:
    MainFrame();
    ~MainFrame() override;

private:
    void OnClose(wxCloseEvent& event);
    void StartClosing();
    void ObserveShutdown(wxTimerEvent& event);

    std::unique_ptr<ApplicationRuntime> runtime_;
    TtsPanel* tts_panel_{};
    ComparePanel* compare_panel_{};
    bool closing_{false};
    wxTimer shutdown_timer_;
};
} // namespace adayo::ui
