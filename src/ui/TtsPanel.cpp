#include "ui/TtsPanel.h"

#include "adapters/tts/SherpaOnnxTtsEngine.h"
#include "ui/CorpusMappingPanel.h"
#include "ui/CorpusRunPanel.h"

#include <filesystem>
#include <memory>

#include <wx/notebook.h>
#include <wx/sizer.h>
#include <wx/stdpaths.h>

namespace adayo::ui {
namespace {
std::filesystem::path ModelsRoot() {
    const std::filesystem::path exe_path(wxStandardPaths::Get().GetExecutablePath().ToStdWstring());
    return exe_path.parent_path() / "models" / "sherpa";
}
} // namespace

TtsPanel::TtsPanel(wxWindow* parent)
    : wxPanel(parent),
      model_registry_(ModelsRoot()),
      playback_service_(tts_service_, audio_player_) {
    tts_service_.SetEngine(std::make_unique<SherpaOnnxTtsEngine>());
    model_scan_ = model_registry_.ScanSherpaModelsWithDiagnostics();

    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* notebook = new wxNotebook(this, wxID_ANY);
    auto* run_panel = new CorpusRunPanel(notebook, &model_scan_.entries, &tts_service_, &playback_service_);
    notebook->AddPage(new CorpusMappingPanel(notebook, run_panel), "列映射", true);
    notebook->AddPage(run_panel, "运行视图", false);
    root->Add(notebook, 1, wxEXPAND);
    SetSizer(root);
}
} // namespace adayo::ui
