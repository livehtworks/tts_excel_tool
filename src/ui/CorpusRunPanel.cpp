#include "ui/CorpusRunPanel.h"

#include "adapters/excel/LibXlsxWriterExporter.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/grid.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

namespace adayo::ui {
namespace {
wxString FromUtf8(const std::string& text) {
    return wxString::FromUTF8(text);
}

std::string ToUtf8(const wxString& text) {
    return text.ToUTF8().data() ? std::string(text.ToUTF8().data()) : std::string{};
}

wxString StateText(PlaybackState state) {
    switch (state) {
        case PlaybackState::Idle: return "空闲";
        case PlaybackState::Generating: return "生成中";
        case PlaybackState::Playing: return "播放中";
        case PlaybackState::Paused: return "已暂停";
        case PlaybackState::Stopping: return "停止中";
        case PlaybackState::Error: return "错误";
    }
    return "未知";
}
} // namespace

CorpusRunPanel::CorpusRunPanel(wxWindow* parent,
    const std::vector<TtsModelEntry>* models,
    TtsService* tts,
    PlaybackService* playback)
    : wxPanel(parent),
      models_(models),
      tts_(tts),
      playback_(playback),
      playback_timer_(this) {
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* top = new wxBoxSizer(wxHORIZONTAL);
    status_ = new wxStaticText(this, wxID_ANY, "尚未生成运行视图");
    auto* export_button = new wxButton(this, wxID_ANY, "导出 Excel");
    export_button->Bind(wxEVT_BUTTON, &CorpusRunPanel::OnExport, this);
    top->Add(status_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    top->Add(export_button, 0);
    root->Add(top, 0, wxEXPAND | wxALL, 6);

    auto* playback_bar = new wxBoxSizer(wxHORIZONTAL);
    play_column_choice_ = new wxChoice(this, wxID_ANY);
    start_row_ = new wxSpinCtrl(this, wxID_ANY);
    end_row_ = new wxSpinCtrl(this, wxID_ANY);
    interval_ms_ = new wxSpinCtrl(this, wxID_ANY);
    speed_ = new wxSpinCtrlDouble(this, wxID_ANY);
    play_button_ = new wxButton(this, wxID_ANY, "播放");
    pause_button_ = new wxButton(this, wxID_ANY, "暂停");
    resume_button_ = new wxButton(this, wxID_ANY, "继续");
    stop_button_ = new wxButton(this, wxID_ANY, "停止");

    start_row_->SetRange(1, 1);
    end_row_->SetRange(1, 1);
    interval_ms_->SetRange(0, 10000);
    interval_ms_->SetValue(250);
    speed_->SetRange(0.5, 2.0);
    speed_->SetIncrement(0.1);
    speed_->SetDigits(1);
    speed_->SetValue(1.0);

    play_button_->Bind(wxEVT_BUTTON, &CorpusRunPanel::OnPlay, this);
    pause_button_->Bind(wxEVT_BUTTON, &CorpusRunPanel::OnPause, this);
    resume_button_->Bind(wxEVT_BUTTON, &CorpusRunPanel::OnResume, this);
    stop_button_->Bind(wxEVT_BUTTON, &CorpusRunPanel::OnStop, this);
    Bind(wxEVT_TIMER, &CorpusRunPanel::OnPlaybackTimer, this);

    playback_bar->Add(play_column_choice_, 1, wxRIGHT, 6);
    playback_bar->Add(start_row_, 0, wxRIGHT, 4);
    playback_bar->Add(end_row_, 0, wxRIGHT, 4);
    playback_bar->Add(interval_ms_, 0, wxRIGHT, 4);
    playback_bar->Add(speed_, 0, wxRIGHT, 6);
    playback_bar->Add(play_button_, 0, wxRIGHT, 4);
    playback_bar->Add(pause_button_, 0, wxRIGHT, 4);
    playback_bar->Add(resume_button_, 0, wxRIGHT, 4);
    playback_bar->Add(stop_button_, 0);
    root->Add(playback_bar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    grid_ = new wxGrid(this, wxID_ANY);
    grid_->CreateGrid(0, 0);
    grid_->EnableEditing(true);
    grid_->Bind(wxEVT_GRID_CELL_CHANGED, &CorpusRunPanel::OnCellChanged, this);
    grid_->Bind(wxEVT_GRID_CELL_LEFT_DCLICK, &CorpusRunPanel::OnCellDClick, this);
    root->Add(grid_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    SetSizer(root);
    UpdatePlaybackUi();
}

void CorpusRunPanel::OnExport(wxCommandEvent&) {
    if (!session_) return;
    wxFileDialog dialog(this, "导出运行视图", "", "runtime.xlsx", "Excel workbook (*.xlsx)|*.xlsx", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() == wxID_OK) {
        try {
            LibXlsxWriterExporter exporter;
            exporter.ExportRuntimeView(session_->view, std::filesystem::path(dialog.GetPath().ToStdWstring()));
            status_->SetLabel("运行视图已导出");
        } catch (const std::exception& ex) {
            status_->SetLabel(FromUtf8(ex.what()));
        }
    }
}

void CorpusRunPanel::SetSession(CorpusSession session) {
    session_ = std::move(session);
    RefreshGrid();
}

void CorpusRunPanel::RefreshGrid() {
    grid_->Freeze();
    if (grid_->GetNumberRows() > 0) {
        grid_->DeleteRows(0, grid_->GetNumberRows());
    }
    if (grid_->GetNumberCols() > 0) {
        grid_->DeleteCols(0, grid_->GetNumberCols());
    }

    if (!session_) {
        status_->SetLabel("尚未生成运行视图");
        PopulatePlayColumns();
        grid_->Thaw();
        return;
    }

    const auto& view = session_->view;
    if (!view.headers.empty()) {
        grid_->AppendCols(static_cast<int>(view.headers.size()));
        for (std::size_t c = 0; c < view.headers.size(); ++c) {
            grid_->SetColLabelValue(static_cast<int>(c), FromUtf8(view.headers[c]));
        }
    }
    if (!view.rows.empty()) {
        grid_->AppendRows(static_cast<int>(view.rows.size()));
        for (std::size_t r = 0; r < view.rows.size(); ++r) {
            for (std::size_t c = 0; c < view.headers.size(); ++c) {
                const std::string value = c < view.rows[r].size() ? view.rows[r][c] : std::string{};
                grid_->SetCellValue(static_cast<int>(r), static_cast<int>(c), FromUtf8(value));
                bool read_only = c < view.columns.size() &&
                    (view.columns[c].role == ColumnRole::Index || view.columns[c].role == ColumnRole::Result);
                if (c < view.columns.size() && view.columns[c].role == ColumnRole::Play && r < view.row_meta.size()) {
                    const auto segment = view.row_meta[r].segment_indexes.find(view.columns[c].source_index);
                    read_only = segment == view.row_meta[r].segment_indexes.end() || !segment->second.has_value();
                }
                if (read_only) {
                    grid_->SetReadOnly(static_cast<int>(r), static_cast<int>(c));
                }
            }
        }
    }
    grid_->AutoSizeColumns(false);
    PopulatePlayColumns();
    status_->SetLabel(wxString::Format("运行视图：%d 行，%d 列", grid_->GetNumberRows(), grid_->GetNumberCols()));
    grid_->Thaw();
    UpdatePlaybackUi();
}

void CorpusRunPanel::PopulatePlayColumns() {
    play_view_columns_.clear();
    if (!play_column_choice_) return;
    play_column_choice_->Clear();
    if (!session_) {
        play_column_choice_->Enable(false);
        return;
    }
    const auto& view = session_->view;
    for (std::size_t c = 0; c < view.columns.size(); ++c) {
        if (view.columns[c].role != ColumnRole::Play) continue;
        play_view_columns_.push_back(c);
        wxString label = FromUtf8(view.columns[c].excel_column);
        if (!view.columns[c].header.empty()) {
            label += " ";
            label += FromUtf8(view.columns[c].header);
        }
        play_column_choice_->Append(label);
    }
    play_column_choice_->Enable(!play_view_columns_.empty());
    if (!play_view_columns_.empty()) {
        play_column_choice_->SetSelection(0);
    }
    const int row_count = (std::max)(1, grid_->GetNumberRows());
    start_row_->SetRange(1, row_count);
    end_row_->SetRange(1, row_count);
    start_row_->SetValue(1);
    end_row_->SetValue(row_count);
}

const TtsModelEntry& CorpusRunPanel::ResolveModel(const SelectedColumn& column) const {
    if (!models_ || !tts_ || !playback_) {
        throw std::runtime_error("播放服务未初始化");
    }
    if (column.tts_model_id.empty()) {
        throw std::runtime_error("MODEL_REQUIRED: 播放列未绑定语音模型");
    }
    const auto it = std::find_if(models_->begin(), models_->end(), [&](const TtsModelEntry& entry) {
        return entry.id == column.tts_model_id;
    });
    if (it == models_->end()) {
        throw std::runtime_error("MODEL_MISSING: 未找到语音模型 " + column.tts_model_id);
    }
    if (!column.language_code.empty() && it->config.language_code != column.language_code) {
        throw std::runtime_error("MODEL_INVALID: 播放列语言与语音模型不一致");
    }
    return *it;
}

void CorpusRunPanel::StartPlayback(std::size_t first_row, std::size_t last_row, std::size_t view_column) {
    if (!session_) return;
    if (view_column >= session_->view.columns.size()) {
        throw std::out_of_range("播放列越界");
    }
    if (first_row >= session_->view.rows.size() || last_row >= session_->view.rows.size() || first_row > last_row) {
        throw std::out_of_range("播放行范围越界");
    }
    const auto& column = session_->view.columns[view_column];
    if (column.role != ColumnRole::Play) {
        throw std::runtime_error("当前列不是播放列");
    }

    const auto& model = ResolveModel(column);
    playback_->Stop();
    tts_->LoadModel(model.config);

    std::vector<PlaybackItem> items;
    items.reserve(last_row - first_row + 1);
    for (std::size_t r = first_row; r <= last_row; ++r) {
        const auto text = view_column < session_->view.rows[r].size() ? session_->view.rows[r][view_column] : std::string{};
        items.push_back(PlaybackItem{
            TtsRequest{text, column.language_code, model.config.speaker_id, speed_->GetValue()},
            r,
            view_column,
        });
    }
    playback_->PlaySequence(std::move(items), std::chrono::milliseconds(interval_ms_->GetValue()), speed_->GetValue());
    playback_timer_.Start(100);
    HighlightPlaybackCell(first_row, view_column);
    UpdatePlaybackUi();
}

void CorpusRunPanel::HighlightPlaybackCell(std::size_t row, std::size_t column) {
    if (!grid_) return;
    if (row >= static_cast<std::size_t>(grid_->GetNumberRows()) ||
        column >= static_cast<std::size_t>(grid_->GetNumberCols())) {
        return;
    }
    const auto r = static_cast<int>(row);
    const auto c = static_cast<int>(column);
    grid_->SetGridCursor(r, c);
    grid_->SelectBlock(r, c, r, c);
    grid_->MakeCellVisible(r, c);
}

void CorpusRunPanel::UpdatePlaybackUi() {
    const bool has_session = session_.has_value() && grid_->GetNumberRows() > 0 && !play_view_columns_.empty();
    const bool has_playback = playback_ != nullptr && tts_ != nullptr && models_ != nullptr;
    const auto state = playback_ ? playback_->State() : PlaybackState::Idle;
    const bool busy = state == PlaybackState::Generating || state == PlaybackState::Playing ||
        state == PlaybackState::Paused || state == PlaybackState::Stopping;

    play_button_->Enable(has_session && has_playback && !busy);
    pause_button_->Enable(has_playback && (state == PlaybackState::Generating || state == PlaybackState::Playing));
    resume_button_->Enable(has_playback && state == PlaybackState::Paused);
    stop_button_->Enable(has_playback && busy);
    play_column_choice_->Enable(has_session && !busy);
    start_row_->Enable(has_session && !busy);
    end_row_->Enable(has_session && !busy);
    interval_ms_->Enable(has_session && !busy);
    speed_->Enable(has_session && !busy);

    if (playback_ && state == PlaybackState::Error) {
        status_->SetLabel(FromUtf8(playback_->LastError()));
    } else if (playback_ && busy) {
        status_->SetLabel("播放状态：" + StateText(state));
    }
}

void CorpusRunPanel::OnPlay(wxCommandEvent&) {
    if (!session_ || play_column_choice_->GetSelection() == wxNOT_FOUND) return;
    try {
        const auto selection = static_cast<std::size_t>(play_column_choice_->GetSelection());
        const auto view_column = play_view_columns_.at(selection);
        const auto row_count = session_->view.rows.size();
        if (row_count == 0) return;
        std::size_t first = static_cast<std::size_t>((std::max)(1, start_row_->GetValue()) - 1);
        std::size_t last = static_cast<std::size_t>((std::max)(1, end_row_->GetValue()) - 1);
        first = (std::min)(first, row_count - 1);
        last = (std::min)(last, row_count - 1);
        if (first > last) std::swap(first, last);
        StartPlayback(first, last, view_column);
    } catch (const std::exception& ex) {
        status_->SetLabel(FromUtf8(ex.what()));
        UpdatePlaybackUi();
    }
}

void CorpusRunPanel::OnPause(wxCommandEvent&) {
    if (playback_) playback_->Pause();
    UpdatePlaybackUi();
}

void CorpusRunPanel::OnResume(wxCommandEvent&) {
    if (playback_) playback_->Resume();
    UpdatePlaybackUi();
}

void CorpusRunPanel::OnStop(wxCommandEvent&) {
    if (playback_) playback_->Stop();
    playback_timer_.Stop();
    UpdatePlaybackUi();
}

void CorpusRunPanel::OnPlaybackTimer(wxTimerEvent&) {
    if (!playback_) return;
    const auto state = playback_->State();
    if (state == PlaybackState::Idle || state == PlaybackState::Error) {
        playback_timer_.Stop();
    } else {
        HighlightPlaybackCell(playback_->CurrentRow(), playback_->CurrentColumn());
    }
    UpdatePlaybackUi();
}

void CorpusRunPanel::OnCellChanged(wxGridEvent& event) {
    if (!session_) {
        event.Skip();
        return;
    }
    try {
        const auto row = static_cast<std::size_t>(event.GetRow());
        const auto col = static_cast<std::size_t>(event.GetCol());
        const auto old_row_count = session_->view.rows.size();
        const auto old_col_count = session_->view.headers.size();
        service_.UpdateDisplayCell(*session_, row, col, ToUtf8(grid_->GetCellValue(event.GetRow(), event.GetCol())));
        if (session_->view.rows.size() != old_row_count || session_->view.headers.size() != old_col_count) {
            RefreshGrid();
        } else {
            grid_->SetCellValue(event.GetRow(), event.GetCol(), FromUtf8(session_->view.rows[row][col]));
        }
    } catch (const std::exception& ex) {
        status_->SetLabel(FromUtf8(ex.what()));
        if (session_ && event.GetRow() >= 0 && event.GetCol() >= 0 &&
            static_cast<std::size_t>(event.GetRow()) < session_->view.rows.size() &&
            static_cast<std::size_t>(event.GetCol()) < session_->view.rows[static_cast<std::size_t>(event.GetRow())].size()) {
            grid_->SetCellValue(event.GetRow(), event.GetCol(),
                FromUtf8(session_->view.rows[static_cast<std::size_t>(event.GetRow())][static_cast<std::size_t>(event.GetCol())]));
        }
    }
}

void CorpusRunPanel::OnCellDClick(wxGridEvent& event) {
    if (!session_) {
        event.Skip();
        return;
    }
    try {
        const auto row = static_cast<std::size_t>(event.GetRow());
        const auto col = static_cast<std::size_t>(event.GetCol());
        if (col < session_->view.columns.size() && session_->view.columns[col].role == ColumnRole::Result) {
            service_.CycleResult(*session_, row, col);
            grid_->SetCellValue(event.GetRow(), event.GetCol(), FromUtf8(session_->view.rows[row][col]));
            return;
        }
        if (col < session_->view.columns.size() && session_->view.columns[col].role == ColumnRole::Play) {
            StartPlayback(row, row, col);
            return;
        }
    } catch (const std::exception& ex) {
        status_->SetLabel(FromUtf8(ex.what()));
        UpdatePlaybackUi();
    }
    event.Skip();
}

} // namespace adayo::ui
