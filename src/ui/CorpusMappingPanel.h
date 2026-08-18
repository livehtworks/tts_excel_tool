#pragma once

#include "core/worker/WorkerQueue.h"
#include "persistence/JsonConfigStore.h"
#include "services/ModelRegistry.h"
#include "services/WorkbookService.h"

#include <memory>
#include <optional>

#include <wx/panel.h>

class wxButton;
class wxChoice;
class wxGrid;
class wxGridEvent;
class wxSpinCtrl;
class wxStaticText;
class wxTextCtrl;

namespace adayo::ui {

class CorpusRunPanel;

class CorpusMappingPanel final : public wxPanel {
public:
    CorpusMappingPanel(wxWindow* parent, CorpusRunPanel* run_panel);

private:
    void OnBrowse(wxCommandEvent& event);
    void OnLoadSheets(wxCommandEvent& event);
    void OnAnalyze(wxCommandEvent& event);
    void OnBuildView(wxCommandEvent& event);
    void OnSaveMapping(wxCommandEvent& event);
    void OnSelectAll(wxCommandEvent& event);
    void OnSelectNone(wxCommandEvent& event);
    void OnApplyRole(wxCommandEvent& event);
    void OnSheetChanged(wxCommandEvent& event);
    void OnGridCellChanged(wxGridEvent& event);

    void AnalyzeCurrentSheet();
    void FillColumnGrid();
    void LoadModelRegistry();
    void SetBusy(bool busy, const wxString& message);
    void ConfigureRowEditors(int row);
    void RefreshRowModelStatus(int row);
    std::vector<std::string> ModelIdsForLanguage(const std::string& language_code) const;
    bool IsKnownModelForLanguage(const std::string& model_id, const std::string& language_code) const;
    std::vector<SelectedColumn> SelectedColumnsFromGrid() const;
    std::string WorkbookPath() const;
    std::string SheetName() const;

    CorpusRunPanel* run_panel_{};
    std::unique_ptr<WorkbookService> workbook_service_;
    JsonConfigStore config_store_;
    AppConfig config_;
    bool config_save_allowed_{true};
    std::string initial_config_warning_;
    std::optional<WorkbookAnalysis> analysis_;
    std::vector<TtsModelEntry> model_entries_;
    std::vector<TtsModelDiagnostic> model_invalid_;
    WorkerQueue worker_;
    bool busy_{false};

    wxTextCtrl* workbook_path_{};
    wxChoice* sheet_choice_{};
    wxSpinCtrl* header_row_{};
    wxGrid* column_grid_{};
    wxChoice* batch_role_{};
    wxButton* load_button_{};
    wxButton* browse_button_{};
    wxButton* analyze_button_{};
    wxButton* build_button_{};
    wxButton* save_button_{};
    wxButton* select_all_button_{};
    wxButton* select_none_button_{};
    wxButton* apply_role_button_{};
    wxStaticText* status_{};
};

} // namespace adayo::ui
