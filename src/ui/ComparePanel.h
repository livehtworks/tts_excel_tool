#pragma once
#include <wx/panel.h>

class wxSpinCtrlDouble;
class wxTextCtrl;
namespace adayo::ui {
class ComparePanel final : public wxPanel {
public:
    explicit ComparePanel(wxWindow* parent);
private:
    wxTextCtrl* reference_path_{};
    wxTextCtrl* actual_path_{};
    wxSpinCtrlDouble* align_threshold_{};
    wxSpinCtrlDouble* pass_threshold_{};
};
} // namespace adayo::ui
