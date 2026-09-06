#include "ui/MainFrame.h"
#include "app/ApplicationRuntime.h"
#include "ui/TtsPanel.h"
#include "ui/ComparePanel.h"
#include "ui/UiString.h"

#include <filesystem>
#include <wx/msgdlg.h>

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
      runtime_(std::make_unique<ApplicationRuntime>(ExeDir())), shutdown_timer_(this) {
    auto* notebook = new wxNotebook(this, wxID_ANY);
    tts_panel_ = new TtsPanel(notebook, *runtime_);
    compare_panel_ = new ComparePanel(notebook, *runtime_);
    notebook->AddPage(tts_panel_, WxUtf8("语料播放"), true);
    notebook->AddPage(compare_panel_, WxUtf8("文本对比"), false);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(notebook, 1, wxEXPAND);
    SetSizer(sizer);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
    Bind(wxEVT_TIMER, &MainFrame::ObserveShutdown, this, shutdown_timer_.GetId());
    CreateStatusBar();
    Centre();
}

MainFrame::~MainFrame() = default;

void MainFrame::OnClose(wxCloseEvent& event) {
    // Normal close remains vetoed until both the leave gate and worker join finish.
    if(event.CanVeto()) event.Veto();
    if (closing_) return;
    if(tts_panel_) tts_panel_->RequestLeave([this] { StartClosing(); });
    else StartClosing();
}

void MainFrame::StartClosing() {
    if(closing_) return;
    closing_ = true;
    try {
        runtime_->RequestShutdown();
        if (tts_panel_) tts_panel_->BeginShutdown();
        if (compare_panel_) compare_panel_->BeginShutdown();
        SetStatusText(WxUtf8(runtime_->ShutdownStatus()));
        shutdown_timer_.Start(100);
    } catch(const std::exception& ex) {
        closing_=false;
        SetStatusText(WxUtf8("停止请求失败："+std::string(ex.what())));
    }
}

void MainFrame::ObserveShutdown(wxTimerEvent&) {
    SetStatusText(WxUtf8(runtime_->ShutdownStatus()));
    if(!runtime_->ShutdownComplete()) return;
    shutdown_timer_.Stop();
    runtime_->Shutdown();
    Destroy();
}

} // namespace adayo::ui
