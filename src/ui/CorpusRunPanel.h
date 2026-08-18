#pragma once

#include "services/CorpusViewService.h"
#include "core/worker/WorkerQueue.h"
#include "services/ModelRegistry.h"
#include "services/PlaybackService.h"
#include "services/TtsService.h"

#include <chrono>
#include <optional>
#include <vector>

#include <wx/timer.h>
#include <wx/panel.h>

class wxButton;
class wxChoice;
class wxGrid;
class wxGridEvent;
class wxSpinCtrl;
class wxSpinCtrlDouble;
class wxStaticText;

namespace adayo::ui {

class CorpusRunPanel final : public wxPanel {
public:
    explicit CorpusRunPanel(wxWindow* parent,
        const std::vector<TtsModelEntry>* models = nullptr,
        TtsService* tts = nullptr,
        PlaybackService* playback = nullptr);

    void SetSession(CorpusSession session);
    const std::optional<CorpusSession>& Session() const { return session_; }

private:
    void RefreshGrid();
    void PopulatePlayColumns();
    void StartPlayback(std::size_t first_row, std::size_t last_row, std::size_t view_column);
    const TtsModelEntry& ResolveModel(const SelectedColumn& column) const;
    void HighlightPlaybackCell(std::size_t row, std::size_t column);
    void UpdatePlaybackUi();
    void OnExport(wxCommandEvent& event);
    void OnPlay(wxCommandEvent& event);
    void OnPause(wxCommandEvent& event);
    void OnResume(wxCommandEvent& event);
    void OnStop(wxCommandEvent& event);
    void OnPlaybackTimer(wxTimerEvent& event);
    void OnCellChanged(wxGridEvent& event);
    void OnCellDClick(wxGridEvent& event);

    CorpusViewService service_;
    std::optional<CorpusSession> session_;
    const std::vector<TtsModelEntry>* models_{};
    TtsService* tts_{};
    PlaybackService* playback_{};
    std::vector<std::size_t> play_view_columns_;
    wxGrid* grid_{};
    wxChoice* play_column_choice_{};
    wxSpinCtrl* start_row_{};
    wxSpinCtrl* end_row_{};
    wxSpinCtrl* interval_ms_{};
    wxSpinCtrlDouble* speed_{};
    wxButton* play_button_{};
    wxButton* pause_button_{};
    wxButton* resume_button_{};
    wxButton* stop_button_{};
    wxButton* export_button_{};
    wxStaticText* status_{};
    wxTimer playback_timer_;
    WorkerQueue worker_;
    bool export_busy_{false};
};

} // namespace adayo::ui
