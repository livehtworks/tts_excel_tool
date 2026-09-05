#include "ui/CorpusRunPanel.h"

#include "adapters/excel/LibXlsxWriterExporter.h"
#include "app/ApplicationRuntime.h"
#include "ui/UiString.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/msgdlg.h>
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/grid.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

namespace adayo::ui {
namespace {
wxString StateText(PlaybackState state) {
    switch (state) {
        case PlaybackState::Idle: return WxUtf8("空闲");
        case PlaybackState::Generating: return WxUtf8("生成中");
        case PlaybackState::Playing: return WxUtf8("播放中");
        case PlaybackState::Paused: return WxUtf8("已暂停");
        case PlaybackState::Stopping: return WxUtf8("停止中");
        case PlaybackState::Error: return WxUtf8("错误");
    }
    return WxUtf8("未知");
}
} // namespace

CorpusRunPanel::CorpusRunPanel(wxWindow* parent, ApplicationRuntime& runtime)
    : wxPanel(parent),
      runtime_(runtime),
      models_(&runtime.ModelScan().entries),
      playback_(&runtime.Playback()),
      playback_timer_(this) {
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* top = new wxBoxSizer(wxHORIZONTAL);
    status_ = new wxStaticText(this, wxID_ANY, WxUtf8("尚未生成运行视图"),wxDefaultPosition,wxDefaultSize,wxST_ELLIPSIZE_END | wxST_NO_AUTORESIZE);
    status_->SetMinSize(wxSize(0,-1));
    export_button_ = new wxButton(this, wxID_ANY, WxUtf8("导出 Excel"));
    export_button_->Bind(wxEVT_BUTTON, &CorpusRunPanel::OnExport, this);
    top->Add(status_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    top->Add(export_button_, 0);
    root->Add(top, 0, wxEXPAND | wxALL, 6);

    auto* cache_bar=new wxBoxSizer(wxHORIZONTAL);
    cache_enabled_=new wxCheckBox(this,wxID_ANY,WxUtf8("音频缓存"));
    cache_enabled_->SetValue(runtime_.ConfigSnapshot().audio_cache.enabled);
    cache_limit_=new wxSpinCtrl(this,wxID_ANY);
    cache_limit_->SetRange(128,16384);
    cache_limit_->SetValue(static_cast<int>(runtime_.ConfigSnapshot().audio_cache.disk_limit_bytes/(1024*1024)));
    cache_status_=new wxStaticText(this,wxID_ANY,wxString{},wxDefaultPosition,wxDefaultSize,wxST_ELLIPSIZE_END | wxST_NO_AUTORESIZE);
    cache_status_->SetMinSize(wxSize(0,-1));
    clear_cache_=new wxButton(this,wxID_ANY,WxUtf8("清空音频缓存"));
    cache_bar->Add(cache_enabled_,0,wxALIGN_CENTER_VERTICAL|wxRIGHT,6);
    cache_bar->Add(new wxStaticText(this,wxID_ANY,WxUtf8("磁盘上限(MiB)")),0,wxALIGN_CENTER_VERTICAL|wxRIGHT,4);
    cache_bar->Add(cache_limit_,0,wxRIGHT,6);
    cache_bar->Add(cache_status_,1,wxALIGN_CENTER_VERTICAL|wxRIGHT,6);
    cache_bar->Add(clear_cache_,0);
    root->Add(cache_bar,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,6);
    auto configure_cache=[this](wxCommandEvent&) {
        if(!runtime_.ConfigSaveAllowed()) return;
        auto options=runtime_.ConfigSnapshot().audio_cache;
        const auto previous=options;
        options.enabled=cache_enabled_->GetValue();
        options.disk_limit_bytes=static_cast<std::uint64_t>(cache_limit_->GetValue())*1024*1024;
        cache_enabled_->Disable(); cache_limit_->Disable();
        runtime_.BackgroundJobs().Submit([this,options,previous](std::stop_token token) {
            std::string error;
            try {
                if (token.stop_requested()) return;
                runtime_.Tts().Cache()->Configure(options);
                runtime_.UpdateConfig([&](AppConfig& config){config.audio_cache=options;});
                runtime_.SaveConfig();
            } catch(const std::exception& ex) {
                error=ex.what();
                try {
                    runtime_.Tts().Cache()->Configure(previous);
                    runtime_.UpdateConfig([&](AppConfig& config){config.audio_cache=previous;});
                } catch(const std::exception& restore) { error+="; restore failed: "+std::string(restore.what()); }
                runtime_.Logger().Warn("cache",error);
            }
            CallAfter([this,error] {
                if(closing_) return;
                cache_enabled_->Enable(runtime_.ConfigSaveAllowed()); cache_limit_->Enable(runtime_.ConfigSaveAllowed());
                cache_enabled_->SetValue(runtime_.ConfigSnapshot().audio_cache.enabled);
                cache_limit_->SetValue(static_cast<int>(runtime_.Tts().Cache()->Stats().active_limit/(1024*1024)));
                UpdateCacheUi(); if(!error.empty()) cache_status_->SetLabel(WxUtf8(error));
            });
        });
    };
    cache_enabled_->Bind(wxEVT_CHECKBOX,configure_cache);
    cache_limit_->Bind(wxEVT_SPINCTRL,configure_cache);
    cache_enabled_->Enable(runtime_.ConfigSaveAllowed()); cache_limit_->Enable(runtime_.ConfigSaveAllowed());
    clear_cache_->Bind(wxEVT_BUTTON,[this](wxCommandEvent&) {
        if(wxMessageBox(WxUtf8("只清本工具的音频缓存，不删除模型、配置或结果，不中断已开始的声音。确认清空？"),WxUtf8("清空音频缓存"),wxYES_NO|wxNO_DEFAULT|wxICON_QUESTION,this)!=wxYES) return;
        runtime_.Tts().Cache()->BeginClear();
        clear_cache_->Disable(); cache_status_->SetLabel(WxUtf8("清理已排队"));
        runtime_.BackgroundJobs().Submit([this](std::stop_token) {
            runtime_.Tts().Cache()->FinishClear();
            CallAfter([this] { if(closing_) return; clear_cache_->Enable(); UpdateCacheUi(); });
        });
    });
    UpdateCacheUi();

    auto* playback_bar = new wxBoxSizer(wxHORIZONTAL);
    play_column_choice_ = new wxChoice(this, wxID_ANY);
    start_row_ = new wxSpinCtrl(this, wxID_ANY);
    end_row_ = new wxSpinCtrl(this, wxID_ANY);
    interval_ms_ = new wxSpinCtrl(this, wxID_ANY);
    speed_ = new wxSpinCtrlDouble(this, wxID_ANY);
    play_button_ = new wxButton(this, wxID_ANY, WxUtf8("播放"));
    pause_button_ = new wxButton(this, wxID_ANY, WxUtf8("暂停"));
    resume_button_ = new wxButton(this, wxID_ANY, WxUtf8("继续"));
    stop_button_ = new wxButton(this, wxID_ANY, WxUtf8("停止"));

    start_row_->SetRange(1, 1);
    end_row_->SetRange(1, 1);
    interval_ms_->SetRange(0, 10000);
    interval_ms_->SetValue(250);
    speed_->SetRange(0.5, 2.0);
    speed_->SetIncrement(0.1);
    speed_->SetDigits(1);
    speed_->SetValue(runtime_.ConfigSnapshot().speech_rate);

    play_button_->Bind(wxEVT_BUTTON, &CorpusRunPanel::OnPlay, this);
    pause_button_->Bind(wxEVT_BUTTON, &CorpusRunPanel::OnPause, this);
    resume_button_->Bind(wxEVT_BUTTON, &CorpusRunPanel::OnResume, this);
    stop_button_->Bind(wxEVT_BUTTON, &CorpusRunPanel::OnStop, this);
    speed_->Bind(wxEVT_SPINCTRLDOUBLE, &CorpusRunPanel::OnPlaybackSettingsChanged, this);
    Bind(wxEVT_TIMER, &CorpusRunPanel::OnPlaybackTimer, this);

    playback_bar->Add(new wxStaticText(this, wxID_ANY, WxUtf8("播放列")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    playback_bar->Add(play_column_choice_, 1, wxRIGHT, 6);
    playback_bar->Add(new wxStaticText(this, wxID_ANY, WxUtf8("起始行")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    playback_bar->Add(start_row_, 0, wxRIGHT, 4);
    playback_bar->Add(new wxStaticText(this, wxID_ANY, WxUtf8("结束行")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    playback_bar->Add(end_row_, 0, wxRIGHT, 4);
    playback_bar->Add(new wxStaticText(this, wxID_ANY, WxUtf8("句间隔(ms)")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    playback_bar->Add(interval_ms_, 0, wxRIGHT, 4);
    playback_bar->Add(new wxStaticText(this, wxID_ANY, WxUtf8("语速")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    playback_bar->Add(speed_, 0, wxRIGHT, 6);
    playback_bar->Add(play_button_, 0, wxRIGHT, 4);
    playback_bar->Add(pause_button_, 0, wxRIGHT, 4);
    playback_bar->Add(resume_button_, 0, wxRIGHT, 4);
    playback_bar->Add(stop_button_, 0);
    root->Add(playback_bar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    grid_ = new wxGrid(this, wxID_ANY);
    grid_->SetDefaultCellOverflow(false);
    grid_->CreateGrid(0, 0);
    grid_->EnableEditing(true);
    grid_->Bind(wxEVT_GRID_CELL_CHANGED, &CorpusRunPanel::OnCellChanged, this);
    grid_->Bind(wxEVT_GRID_CELL_LEFT_CLICK, &CorpusRunPanel::OnCellClick, this);
    root->Add(grid_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    SetSizer(root);
    UpdatePlaybackUi();
}

void CorpusRunPanel::OnExport(wxCommandEvent&) {
    if (!session_ || export_busy_) return;
    wxFileDialog dialog(this, WxUtf8("导出运行视图"), wxString{}, WxUtf8("runtime.xlsx"), WxUtf8("Excel workbook (*.xlsx)|*.xlsx"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() == wxID_OK) {
        const auto view = session_->view;
        const auto output = PathFromWx(dialog.GetPath());
        export_busy_ = true;
        export_button_->Enable(false);
        status_->SetLabel(WxUtf8("运行视图导出中"));
        runtime_.BackgroundJobs().Submit([this, view, output](std::stop_token token) {
            try {
                if (token.stop_requested()) return;
                LibXlsxWriterExporter exporter;
                exporter.ExportRuntimeView(view, output);
                if (token.stop_requested()) return;
                CallAfter([this] {
                    if (closing_ || IsBeingDeleted()) return;
                    export_busy_ = false;
                    export_button_->Enable(true);
                    status_->SetLabel(WxUtf8("运行视图已导出"));
                });
            } catch (const std::exception& ex) {
                const std::string error = ex.what();
                runtime_.Logger().Error("export", error);
                CallAfter([this, error] {
                    if (closing_ || IsBeingDeleted()) return;
                    export_busy_ = false;
                    export_button_->Enable(true);
                    status_->SetLabel(WxUtf8(error));
                });
            }
        });
    }
}

void CorpusRunPanel::BeginShutdown() {
    if (closing_) return;
    closing_ = true;
    playback_timer_.Stop();
    if (playback_) playback_->Stop();
    Disable();
}

void CorpusRunPanel::UpdateCacheUi() {
    if(!runtime_.Tts().Cache()) return;
    const auto stats=runtime_.Tts().Cache()->Stats();
    const auto label=(stats.accounting_valid?std::to_string(stats.used_bytes/(1024*1024)):std::string("占用未确认"))+" / "+std::to_string(stats.active_limit/(1024*1024))+" MiB, "+
        std::to_string(stats.entries)+" 条"+(stats.clearing ? "，清理中" : "")+(stats.warning.empty() ? "" : "，"+stats.warning);
    cache_status_->SetLabel(WxUtf8(label));
    cache_status_->SetToolTip(WxUtf8(label));
}

void CorpusRunPanel::SetSession(CorpusSession session) {
    ++playback_ui_generation_;
    if (playback_) playback_->Stop();
    playback_timer_.Stop();
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
        status_->SetLabel(WxUtf8("尚未生成运行视图"));
        PopulatePlayColumns();
        grid_->Thaw();
        return;
    }

    const auto& view = session_->view;
    if (!view.headers.empty()) {
        grid_->AppendCols(static_cast<int>(view.headers.size()));
        for (std::size_t c = 0; c < view.headers.size(); ++c) {
            grid_->SetColLabelValue(static_cast<int>(c), WxUtf8(view.headers[c]));
        }
    }
    if (!view.rows.empty()) {
        grid_->AppendRows(static_cast<int>(view.rows.size()));
        for (std::size_t r = 0; r < view.rows.size(); ++r) {
            for (std::size_t c = 0; c < view.headers.size(); ++c) {
                const std::string value = c < view.rows[r].size() ? view.rows[r][c] : std::string{};
                grid_->SetCellValue(static_cast<int>(r), static_cast<int>(c), WxUtf8(value));
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
    for (int c = 0; c < grid_->GetNumberCols(); ++c) {
        grid_->SetColSize(c, c == 0 ? 60 : 180);
    }
    PopulatePlayColumns();
    status_->SetLabel(WxUtf8("运行视图：") + wxString::Format("%d", grid_->GetNumberRows()) + WxUtf8(" 行，") + wxString::Format("%d", grid_->GetNumberCols()) + WxUtf8(" 列"));
    status_->SetLabel(status_->GetLabel()+WxUtf8(" | "+view.source.path+" | "+view.source.sheet+" | header="+std::to_string(view.source.header_row)));
    status_->SetToolTip(WxUtf8(view.source.path+"\n"+view.source.sha256));
    if(!view.diagnostics.empty()) status_->SetLabel(status_->GetLabel()+WxUtf8(" | "+view.diagnostics.front()));
    grid_->Thaw();
    UpdatePlaybackUi();
}

bool CorpusRunPanel::HasPlayableSegment(std::size_t row, std::size_t view_column) const {
    if (!session_ || row >= session_->view.row_meta.size() || view_column >= session_->view.columns.size()) return false;
    const auto& column = session_->view.columns[view_column];
    if (column.role != ColumnRole::Play) return false;
    const auto segment = session_->view.row_meta[row].segment_indexes.find(column.source_index);
    return segment != session_->view.row_meta[row].segment_indexes.end() && segment->second.has_value();
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
        wxString label = WxUtf8(view.columns[c].excel_column);
        if (!view.columns[c].header.empty()) {
            label += " ";
            label += WxUtf8(view.columns[c].header);
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
    if (!models_ || !playback_) {
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

    PlaybackRequest request;
    request.model_id = model.id;
    request.model_config = model.config;
    request.interval = std::chrono::milliseconds(interval_ms_->GetValue());
    request.speed = speed_->GetValue();
    request.items.reserve(last_row - first_row + 1);
    for (std::size_t r = first_row; r <= last_row; ++r) {
        const auto text = view_column < session_->view.rows[r].size() ? session_->view.rows[r][view_column] : std::string{};
        request.items.push_back(PlaybackItem{
            TtsRequest{text, column.language_code, model.config.speaker_id, speed_->GetValue()},
            r,
            view_column,
        });
    }
    playback_->Play(std::move(request));
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
    const bool has_playback = playback_ != nullptr && models_ != nullptr;
    const auto state = playback_ ? playback_->State() : PlaybackState::Idle;
    const bool busy = state == PlaybackState::Generating || state == PlaybackState::Playing ||
        state == PlaybackState::Paused || state == PlaybackState::Stopping;

    grid_->EnableEditing(!busy);
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
        status_->SetLabel(WxUtf8(playback_->LastError()));
    } else if (playback_ && (busy || state != last_playback_state_)) {
        status_->SetLabel(WxUtf8("播放状态：") + StateText(state));
    }
    last_playback_state_ = state;
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
        status_->SetLabel(WxUtf8(ex.what()));
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
    playback_timer_.Start(100);
    UpdatePlaybackUi();
}

void CorpusRunPanel::OnPlaybackSettingsChanged(wxCommandEvent& event) {
    try {
        runtime_.UpdateConfig([&](AppConfig& config) {
            config.speech_rate = speed_->GetValue();
        });
        runtime_.SaveConfig();
    } catch (const std::exception& ex) {
        runtime_.Logger().Error("config", ex.what());
        status_->SetLabel(WxUtf8(ex.what()));
    }
    event.Skip();
}

void CorpusRunPanel::OnPlaybackTimer(wxTimerEvent&) {
    UpdateCacheUi();
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
        const auto state = playback_ ? playback_->State() : PlaybackState::Idle;
        if (state == PlaybackState::Generating || state == PlaybackState::Playing ||
            state == PlaybackState::Paused || state == PlaybackState::Stopping) {
            status_->SetLabel(WxUtf8("播放中禁止编辑文本"));
            return;
        }
        const auto row = static_cast<std::size_t>(event.GetRow());
        const auto col = static_cast<std::size_t>(event.GetCol());
        const auto old_row_count = session_->view.rows.size();
        const auto old_col_count = session_->view.headers.size();
        const auto choice=play_column_choice_->GetSelection();
        const auto play_source=choice>=0 ? session_->view.columns[play_view_columns_.at(choice)].source_index : std::size_t{};
        auto identity=[&](int display)->std::optional<ResultIdentity> {
            if(display<0 || static_cast<std::size_t>(display)>=session_->view.row_meta.size()) return {};
            const auto& meta=session_->view.row_meta[display];
            const auto segment=meta.segment_indexes.find(play_source);
            return ResultIdentity{meta.raw_row_index,play_source,segment!=meta.segment_indexes.end() && segment->second ? *segment->second : meta.expanded_index};
        };
        const auto first=identity(start_row_->GetValue()-1),last=identity(end_row_->GetValue()-1),cursor=identity(grid_->GetGridCursorRow());
        const auto cursor_column=grid_->GetGridCursorCol();
        int scroll_x=0,scroll_y=0; grid_->GetViewStart(&scroll_x,&scroll_y);
        const auto impact=service_.UpdateDisplayCell(*session_, row, col, Utf8FromWx(grid_->GetCellValue(event.GetRow(), event.GetCol())));
        if (session_->view.rows.size() != old_row_count || session_->view.headers.size() != old_col_count || impact.segment_structure_changed) {
            RefreshGrid();
            for(std::size_t i=0;i<play_view_columns_.size();++i) if(session_->view.columns[play_view_columns_[i]].source_index==play_source) play_column_choice_->SetSelection(static_cast<int>(i));
            auto restore=[&](const std::optional<ResultIdentity>& saved) {
                int nearest=0;
                if(!saved) return nearest;
                for(std::size_t i=0;i<session_->view.row_meta.size();++i) {
                    const auto& meta=session_->view.row_meta[i];
                    if(meta.raw_row_index>saved->raw_row_index) break;
                    nearest=static_cast<int>(i);
                    if(meta.raw_row_index==saved->raw_row_index && meta.expanded_index>=saved->segment_index) break;
                }
                return nearest;
            };
            start_row_->SetValue(restore(first)+1); end_row_->SetValue((std::max)(start_row_->GetValue(),restore(last)+1));
            if(!session_->view.rows.empty() && grid_->GetNumberCols()>0) grid_->SetGridCursor(restore(cursor),std::clamp(cursor_column,0,grid_->GetNumberCols()-1));
            grid_->Scroll(scroll_x,scroll_y);
        } else {
            for(auto affected:impact.display_rows) for (std::size_t c = 0; c < session_->view.rows[affected].size(); ++c) {
                grid_->SetCellValue(static_cast<int>(affected), static_cast<int>(c), WxUtf8(session_->view.rows[affected][c]));
            }
        }
    } catch (const std::exception& ex) {
        status_->SetLabel(WxUtf8(ex.what()));
        if (session_ && event.GetRow() >= 0 && event.GetCol() >= 0 &&
            static_cast<std::size_t>(event.GetRow()) < session_->view.rows.size() &&
            static_cast<std::size_t>(event.GetCol()) < session_->view.rows[static_cast<std::size_t>(event.GetRow())].size()) {
            grid_->SetCellValue(event.GetRow(), event.GetCol(),
                WxUtf8(session_->view.rows[static_cast<std::size_t>(event.GetRow())][static_cast<std::size_t>(event.GetCol())]));
        }
    }
}

void CorpusRunPanel::OnCellClick(wxGridEvent& event) {
    if (!session_) {
        event.Skip();
        return;
    }
    try {
        const auto row = static_cast<std::size_t>(event.GetRow());
        const auto col = static_cast<std::size_t>(event.GetCol());
        if (col < session_->view.columns.size() && session_->view.columns[col].role == ColumnRole::Result) {
            service_.CycleResult(*session_, row, col);
            grid_->SetCellValue(event.GetRow(), event.GetCol(), WxUtf8(session_->view.rows[row][col]));
            return;
        }
        if (col < session_->view.columns.size() && session_->view.columns[col].role == ColumnRole::Play) {
            if (!HasPlayableSegment(row, col)) return;
            StartPlayback(row, row, col);
            return;
        }
    } catch (const std::exception& ex) {
        status_->SetLabel(WxUtf8(ex.what()));
        UpdatePlaybackUi();
    }
    event.Skip();
}

} // namespace adayo::ui
