#pragma once

#include "services/CorpusViewService.h"
#include "services/ModelRegistry.h"
#include "services/PlaybackService.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>
#include <functional>

#include <wx/timer.h>
#include <wx/panel.h>

class wxButton;
class wxCheckBox;
class wxChoice;
class wxGrid;
class wxGridEvent;
class wxSpinCtrl;
class wxSpinCtrlDouble;
class wxStaticText;
class wxTextCtrl;

namespace adayo {
class ApplicationRuntime;
}

namespace adayo::ui {
class CorpusRunPanel final : public wxPanel {
public:
    CorpusRunPanel(wxWindow* parent, ApplicationRuntime& runtime);

    void SetSession(CorpusSession session, std::function<void()> installed = {});
    void RequestLeave(std::function<void()> action);
    void SetSessionInstalledHandler(std::function<void()> handler) { session_installed_=std::move(handler); }
    const std::optional<CorpusSession>& Session() const { return session_; }
    void BeginShutdown();

private:
    void RefreshGrid();
    void PopulatePlayColumns();
    void StartPlayback(std::size_t first_row, std::size_t last_row, std::size_t view_column);
    const TtsModelEntry& ResolveModel(const SelectedColumn& column) const;
    bool HasPlayableSegment(std::size_t row, std::size_t view_column) const;
    void HighlightPlaybackCell(std::size_t row, std::size_t column);
    void UpdatePlaybackUi();
    void OnExport(wxCommandEvent& event);
    bool StartExport();
    void FinishExport(std::uint64_t session_id, std::uint64_t revision, const std::string& error);
    void OnPlay(wxCommandEvent& event);
    void OnPause(wxCommandEvent& event);
    void OnResume(wxCommandEvent& event);
    void OnStop(wxCommandEvent& event);
    void OnPlaybackTimer(wxTimerEvent& event);
    void OnPlaybackSettingsChanged(wxCommandEvent& event);
    void ApplyCellEdit(int row,int column,const std::string& value);
    void EditCell();
    void ShowCellDetails(int row,int column);
    void ClearPlaybackHighlight();
    void OnCellClick(wxGridEvent& event);
    void UpdateCacheUi();
    wxCheckBox* cache_enabled_{};
    wxSpinCtrl* cache_limit_{};
    wxStaticText* cache_status_{};
    wxButton* clear_cache_{};
    wxButton* apply_cache_{};
    wxCheckBox* follow_playback_{};
    wxButton* edit_cell_{};
    wxTextCtrl* cell_details_{};
    std::optional<std::pair<int,int>> highlighted_cell_;
    bool cache_busy_{};

    ApplicationRuntime& runtime_;
    CorpusViewService service_;
    std::optional<CorpusSession> session_;
    const std::vector<TtsModelEntry>* models_{};
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
    bool export_busy_{false};
    bool closing_{false};
    PlaybackState last_playback_state_{PlaybackState::Idle};
    std::uint64_t playback_ui_generation_{0};
    std::uint64_t playback_session_id_{0}, playback_request_id_{0};
    std::function<void()> pending_action_, session_installed_;
};

} // namespace adayo::ui
