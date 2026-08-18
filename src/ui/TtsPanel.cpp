#include "ui/TtsPanel.h"

#include "ui/CorpusMappingPanel.h"
#include "ui/CorpusRunPanel.h"

#include <wx/notebook.h>
#include <wx/sizer.h>

namespace adayo::ui {
TtsPanel::TtsPanel(wxWindow* parent) : wxPanel(parent) {
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* notebook = new wxNotebook(this, wxID_ANY);
    auto* run_panel = new CorpusRunPanel(notebook);
    notebook->AddPage(new CorpusMappingPanel(notebook, run_panel), "列映射", true);
    notebook->AddPage(run_panel, "运行视图", false);
    root->Add(notebook, 1, wxEXPAND);
    SetSizer(root);
}
} // namespace adayo::ui
