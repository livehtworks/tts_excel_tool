#pragma once

#include <wx/panel.h>

class wxButton;
class wxChoice;
class wxSpinCtrlDouble;
class wxTextCtrl;

namespace adayo::ui {
class TtsPanel final : public wxPanel {
public:
    explicit TtsPanel(wxWindow* parent);
private:
    wxTextCtrl* workbook_path_{};
    wxChoice* sheet_choice_{};
    wxTextCtrl* text_input_{};
    wxChoice* language_choice_{};
    wxChoice* voice_choice_{};
    wxSpinCtrlDouble* speed_{};
};
} // namespace adayo::ui
