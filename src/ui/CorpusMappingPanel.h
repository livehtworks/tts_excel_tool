#pragma once

#include "persistence/JsonConfigStore.h"
#include "services/WorkbookService.h"

#include <memory>
#include <optional>

#include <wx/panel.h>

class wxButton;
class wxChoice;
class wxGrid;
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

    void AnalyzeCurrentSheet();
    void FillColumnGrid();
    std::vector<SelectedColumn> SelectedColumnsFromGrid() const;
    std::string WorkbookPath() const;
    std::string SheetName() const;

    CorpusRunPanel* run_panel_{};
    std::unique_ptr<WorkbookService> workbook_service_;
    JsonConfigStore config_store_;
    AppConfig config_;
    std::optional<WorkbookAnalysis> analysis_;

    wxTextCtrl* workbook_path_{};
    wxChoice* sheet_choice_{};
    wxSpinCtrl* header_row_{};
    wxGrid* column_grid_{};
    wxStaticText* status_{};
};

} // namespace adayo::ui
