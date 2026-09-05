#pragma once

#include <memory>

#include <wx/frame.h>

namespace adayo {
class ApplicationRuntime;
}

namespace adayo::ui {
class TtsPanel;
class ComparePanel;

class MainFrame final : public wxFrame {
public:
    MainFrame();

private:
    void OnClose(wxCloseEvent& event);

    std::unique_ptr<ApplicationRuntime> runtime_;
    TtsPanel* tts_panel_{};
    ComparePanel* compare_panel_{};
    bool closing_{false};
};
} // namespace adayo::ui
