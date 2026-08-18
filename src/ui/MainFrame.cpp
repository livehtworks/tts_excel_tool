#include "ui/MainFrame.h"
#include "ui/TtsPanel.h"
#include "ui/ComparePanel.h"

#include <wx/notebook.h>
#include <wx/sizer.h>

namespace adayo::ui {
MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "Adayo语料测试", wxDefaultPosition, wxSize(1280, 820)) {
    auto* notebook = new wxNotebook(this, wxID_ANY);
    notebook->AddPage(new TtsPanel(notebook), "语料播放", true);
    notebook->AddPage(new ComparePanel(notebook), "文本对比", false);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(notebook, 1, wxEXPAND);
    SetSizer(sizer);
    Centre();
}
} // namespace adayo::ui
