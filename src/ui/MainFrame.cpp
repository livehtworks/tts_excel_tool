#include "ui/MainFrame.h"
#include "app/ApplicationRuntime.h"
#include "ui/TtsPanel.h"
#include "ui/ComparePanel.h"
#include "ui/UiString.h"

#include <filesystem>

#include <wx/event.h>
#include <wx/notebook.h>
#include <wx/sizer.h>
#include <wx/stdpaths.h>

namespace adayo::ui {
namespace {
std::filesystem::path ExeDir() {
    return PathFromWx(wxStandardPaths::Get().GetExecutablePath()).parent_path();
}
} // namespace

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, WxUtf8("Adayo语料测试"), wxDefaultPosition, wxSize(1280, 820)),
      runtime_(std::make_unique<ApplicationRuntime>(ExeDir())) {
    auto* notebook = new wxNotebook(this, wxID_ANY);
    tts_panel_ = new TtsPanel(notebook, *runtime_);
    compare_panel_ = new ComparePanel(notebook, *runtime_);
    notebook->AddPage(tts_panel_, WxUtf8("语料播放"), true);
    notebook->AddPage(compare_panel_, WxUtf8("文本对比"), false);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(notebook, 1, wxEXPAND);
    SetSizer(sizer);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
    Centre();
}

void MainFrame::OnClose(wxCloseEvent&) {
    if (closing_) return;
    closing_ = true;
    if (tts_panel_) tts_panel_->BeginShutdown();
    if (compare_panel_) compare_panel_->BeginShutdown();
    if (runtime_) runtime_->Shutdown();
    Destroy();
}

} // namespace adayo::ui
