#pragma once

#include "services/ModelRegistry.h"
#include "services/WorkbookService.h"

#include <optional>
#include <vector>

#include <wx/arrstr.h>
#include <wx/panel.h>

class wxButton;
class wxCheckBox;
class wxChoice;
class wxGrid;
class wxGridEvent;
class wxSpinCtrl;
class wxStaticText;
class wxTextCtrl;

namespace adayo {
class ApplicationRuntime;
}

namespace adayo::ui {
class CorpusRunPanel;

class CorpusMappingPanel final : public wxPanel {
public:
    CorpusMappingPanel(wxWindow* parent, ApplicationRuntime& runtime, CorpusRunPanel* run_panel);
    void BeginShutdown();

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

    bool CanUseUi() const;
    void AnalyzeCurrentSheet();
    void FillColumnGrid();
    void RefreshModelRegistry();
    void SetBusy(bool busy, const wxString& message);
    void ConfigureRowEditors(int row);
    void RefreshRowModelStatus(int row);
    wxArrayString LanguageChoices() const;
    std::string GridFixedLanguage(int row) const;
    std::string EffectiveLanguageForRow(int row) const;
    std::vector<std::string> ModelIdsForLanguage(const std::string& language_code) const;
    bool IsKnownModelForLanguage(const std::string& model_id, const std::string& language_code) const;
    std::vector<SelectedColumn> SelectedColumnsFromGrid() const;
    std::filesystem::path WorkbookPath() const;
    std::string SheetName() const;
    void InvalidateAnalysis();
    bool AnalysisMatchesInput() const;
    void RefreshSheetChoices();
    std::uint64_t input_revision_{}, analysis_revision_{};
    std::vector<WorksheetInfo> sheets_;
    std::vector<std::string> displayed_sheets_;
    wxCheckBox* show_hidden_{};

    ApplicationRuntime& runtime_;
    CorpusRunPanel* run_panel_{};
    std::optional<WorkbookAnalysis> analysis_;
    std::vector<TtsModelEntry> model_entries_;
    std::vector<TtsModelDiagnostic> model_invalid_;
    bool busy_{false};
    bool closing_{false};

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
