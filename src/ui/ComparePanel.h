#pragma once
#include "core/worker/WorkerQueue.h"
#include "core/domain/Types.h"
#include "services/CompareService.h"

#include <filesystem>

#include <wx/panel.h>

class wxButton;
class wxCheckBox;
class wxChoice;
class wxCommandEvent;
class wxGrid;
class wxListBox;
class wxSpinCtrlDouble;
class wxStaticText;
class wxTextCtrl;
namespace adayo::ui {
class ComparePanel final : public wxPanel {
public:
    explicit ComparePanel(wxWindow* parent);
private:
    struct CompareInputGroup {
        std::string label;
        std::filesystem::path reference_path;
        std::filesystem::path actual_path;
    };

    void OnOpenReference(wxCommandEvent& event);
    void OnOpenActual(wxCommandEvent& event);
    void OnAddGroup(wxCommandEvent& event);
    void OnRemoveGroup(wxCommandEvent& event);
    void OnCompare(wxCommandEvent& event);
    void OnExport(wxCommandEvent& event);

    void LoadFileInto(wxTextCtrl* target);
    std::vector<CompareInputGroup> CurrentInputGroups() const;
    CompareOptions CurrentOptions() const;
    std::string CurrentDelimiter() const;
    void RefreshGroups();
    void RefreshGrid();
    void SetBusy(bool busy, const wxString& message);
    void SetStatus(const wxString& message);

    WorkerQueue worker_;
    wxTextCtrl* language_label_{};
    wxTextCtrl* reference_path_{};
    wxTextCtrl* actual_path_{};
    wxTextCtrl* delimiter_{};
    wxSpinCtrlDouble* align_threshold_{};
    wxSpinCtrlDouble* pass_threshold_{};
    wxCheckBox* ignore_punctuation_{};
    wxButton* add_group_{};
    wxButton* remove_group_{};
    wxButton* compare_button_{};
    wxButton* export_button_{};
    wxStaticText* status_{};
    wxListBox* group_list_{};
    wxGrid* result_grid_{};
    std::vector<CompareInputGroup> input_groups_;
    std::vector<CompareReportGroup> report_groups_;
    bool busy_{false};
};
} // namespace adayo::ui
