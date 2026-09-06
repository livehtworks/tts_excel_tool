#include "ui/TtsPanel.h"

#include "app/ApplicationRuntime.h"
#include "ui/CorpusMappingPanel.h"
#include "ui/CorpusRunPanel.h"
#include "ui/UiString.h"

#include <wx/notebook.h>
#include <wx/sizer.h>

namespace adayo::ui {

TtsPanel::TtsPanel(wxWindow* parent, ApplicationRuntime& runtime)
    : wxPanel(parent),
      runtime_(runtime) {
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* notebook = new wxNotebook(this, wxID_ANY);
    run_panel_ = new CorpusRunPanel(notebook, runtime_);
    mapping_panel_ = new CorpusMappingPanel(notebook, runtime_, run_panel_);
    notebook->AddPage(mapping_panel_, WxUtf8("列映射"), true);
    notebook->AddPage(run_panel_, WxUtf8("运行视图"), false);
    run_panel_->SetSessionInstalledHandler([notebook] { notebook->SetSelection(1); });
    root->Add(notebook, 1, wxEXPAND);
    SetSizer(root);
}

void TtsPanel::BeginShutdown() {
    if (closing_) return;
    closing_ = true;
    if (mapping_panel_) mapping_panel_->BeginShutdown();
    if (run_panel_) run_panel_->BeginShutdown();
    Disable();
}

void TtsPanel::RequestLeave(std::function<void()> action) {
    if(!closing_) run_panel_->RequestLeave(std::move(action));
}

} // namespace adayo::ui
