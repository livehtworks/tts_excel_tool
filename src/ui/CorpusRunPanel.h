#pragma once

#include "services/CorpusViewService.h"

#include <optional>

#include <wx/panel.h>

class wxButton;
class wxGrid;
class wxGridEvent;
class wxStaticText;

namespace adayo::ui {

class CorpusRunPanel final : public wxPanel {
public:
    explicit CorpusRunPanel(wxWindow* parent);

    void SetSession(CorpusSession session);
    const std::optional<CorpusSession>& Session() const { return session_; }

private:
    void RefreshGrid();
    void OnCellChanged(wxGridEvent& event);
    void OnCellDClick(wxGridEvent& event);

    CorpusViewService service_;
    std::optional<CorpusSession> session_;
    wxGrid* grid_{};
    wxStaticText* status_{};
};

} // namespace adayo::ui
