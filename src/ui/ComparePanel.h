#pragma once
#include "core/domain/Types.h"
#include "services/CompareService.h"

#include <wx/panel.h>

class wxCheckBox;
class wxCommandEvent;
class wxGrid;
class wxSpinCtrlDouble;
class wxTextCtrl;
namespace adayo::ui {
class ComparePanel final : public wxPanel {
public:
    explicit ComparePanel(wxWindow* parent);
private:
    void OnOpenReference(wxCommandEvent& event);
    void OnOpenActual(wxCommandEvent& event);
    void OnCompare(wxCommandEvent& event);
    void OnExport(wxCommandEvent& event);

    void LoadFileInto(wxTextCtrl* target);
    void RefreshGrid();
    static std::vector<std::string> ReadLines(const wxString& path);

    wxTextCtrl* reference_path_{};
    wxTextCtrl* actual_path_{};
    wxSpinCtrlDouble* align_threshold_{};
    wxSpinCtrlDouble* pass_threshold_{};
    wxCheckBox* ignore_punctuation_{};
    wxGrid* result_grid_{};
    std::vector<CompareRow> rows_;
};
} // namespace adayo::ui
