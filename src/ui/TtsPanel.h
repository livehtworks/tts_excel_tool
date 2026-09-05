#pragma once

#include <wx/panel.h>

namespace adayo {
class ApplicationRuntime;
}

namespace adayo::ui {
class CorpusMappingPanel;
class CorpusRunPanel;

class TtsPanel final : public wxPanel {
public:
    TtsPanel(wxWindow* parent, ApplicationRuntime& runtime);
    void BeginShutdown();

private:
    ApplicationRuntime& runtime_;
    CorpusMappingPanel* mapping_panel_{};
    CorpusRunPanel* run_panel_{};
    bool closing_{false};
};
} // namespace adayo::ui
