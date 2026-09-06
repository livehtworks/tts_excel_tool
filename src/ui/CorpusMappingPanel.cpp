#include "ui/CorpusMappingPanel.h"

#include "adapters/excel/OpenXlsxWorkbookReader.h"
#include "app/ApplicationRuntime.h"
#include "services/CorpusViewService.h"
#include "services/ModelRegistry.h"
#include "ui/CorpusRunPanel.h"
#include "ui/VoiceSelectionDialog.h"
#include "ui/UiString.h"
#include "platform/UnicodePath.h"

#include <filesystem>
#include <algorithm>
#include <set>
#include <stdexcept>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/grid.h>
#include <wx/scopeguard.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/wupdlock.h>

namespace adayo::ui {
namespace {
const char* kAutoLanguageChoice = "自动识别";

const char* TypeText(SuggestedColumnType type) {
    switch (type) {
        case SuggestedColumnType::Unknown: return "unknown";
        case SuggestedColumnType::Meta: return "meta";
        case SuggestedColumnType::Utterance: return "utterance";
        case SuggestedColumnType::Result: return "result";
    }
    return "unknown";
}

const char* RoleText(ColumnRole role) {
    switch (role) {
        case ColumnRole::Ignore: return "忽略";
        case ColumnRole::Reference: return "参考";
        case ColumnRole::Play: return "播放";
        case ColumnRole::Result: return "结果";
        case ColumnRole::Index: return "序号";
    }
    return "忽略";
}

ColumnRole RoleFromText(std::string text) {
    if (text == "参考") return ColumnRole::Reference;
    if (text == "播放") return ColumnRole::Play;
    if (text == "结果") return ColumnRole::Result;
    if (text == "序号") return ColumnRole::Index;
    if (text == "忽略") return ColumnRole::Ignore;
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
    });
    if (text == "reference" || text == "ref") return ColumnRole::Reference;
    if (text == "play") return ColumnRole::Play;
    if (text == "result") return ColumnRole::Result;
    return ColumnRole::Ignore;
}

wxArrayString RoleChoices() {
    wxArrayString values;
    values.Add(WxUtf8("忽略"));
    values.Add(WxUtf8("参考"));
    values.Add(WxUtf8("播放"));
    return values;
}

} // namespace

CorpusMappingPanel::CorpusMappingPanel(wxWindow* parent, ApplicationRuntime& runtime, CorpusRunPanel* run_panel)
    : wxPanel(parent),
      runtime_(runtime),
      run_panel_(run_panel) {

    const auto config = runtime_.ConfigSnapshot();
    RefreshModelRegistry();

    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* file_row = new wxBoxSizer(wxHORIZONTAL);
    workbook_path_ = new wxTextCtrl(this, wxID_ANY, WxUtf8(config.last_workbook));
    browse_button_ = new wxButton(this, wxID_ANY, WxUtf8("浏览"));
    load_button_ = new wxButton(this, wxID_ANY, WxUtf8("读取 Sheet"));
    file_row->Add(new wxStaticText(this, wxID_ANY, WxUtf8("Excel")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    file_row->Add(workbook_path_, 1, wxRIGHT, 6);
    file_row->Add(browse_button_, 0, wxRIGHT, 6);
    file_row->Add(load_button_, 0);
    root->Add(file_row, 0, wxEXPAND | wxALL, 6);

    auto* sheet_row = new wxBoxSizer(wxHORIZONTAL);
    sheet_choice_ = new wxChoice(this, wxID_ANY);
    header_row_ = new wxSpinCtrl(this, wxID_ANY);
    header_row_->SetRange(1, 1000);
    header_row_->SetValue(1);
    analyze_button_ = new wxButton(this, wxID_ANY, WxUtf8("分析列"));
    build_button_ = new wxButton(this, wxID_ANY, WxUtf8("生成运行视图"));
    save_button_ = new wxButton(this, wxID_ANY, WxUtf8("保存映射"));
    select_all_button_ = new wxButton(this, wxID_ANY, WxUtf8("全选"));
    select_none_button_ = new wxButton(this, wxID_ANY, WxUtf8("全不选"));
    batch_role_ = new wxChoice(this, wxID_ANY);
    batch_role_->Append(WxUtf8("忽略"));
    batch_role_->Append(WxUtf8("参考"));
    batch_role_->Append(WxUtf8("播放"));
    batch_role_->SetSelection(0);
    apply_role_button_ = new wxButton(this, wxID_ANY, WxUtf8("批量用途"));
    sheet_row->Add(new wxStaticText(this, wxID_ANY, WxUtf8("Sheet")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    sheet_row->Add(sheet_choice_, 1, wxRIGHT, 12);
    sheet_row->Add(new wxStaticText(this, wxID_ANY, WxUtf8("表头行")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    sheet_row->Add(header_row_, 0, wxRIGHT, 12);
    sheet_row->Add(analyze_button_, 0, wxRIGHT, 6);
    root->Add(sheet_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    auto* actions = new wxBoxSizer(wxHORIZONTAL);
    actions->Add(build_button_, 0, wxRIGHT, 6);
    actions->Add(save_button_, 0, wxRIGHT, 12);
    actions->Add(select_all_button_, 0, wxRIGHT, 6);
    actions->Add(select_none_button_, 0, wxRIGHT, 6);
    actions->Add(batch_role_, 0, wxRIGHT, 6);
    actions->Add(apply_role_button_, 0);
    root->Add(actions, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    show_hidden_=new wxCheckBox(this,wxID_ANY,WxUtf8("显示隐藏工作表"));
    root->Add(show_hidden_,0,wxLEFT|wxRIGHT|wxBOTTOM,6);

    column_grid_ = new wxGrid(this, wxID_ANY);
    column_grid_->SetDefaultCellOverflow(false);
    column_grid_->CreateGrid(0, ColumnCount);
    const char* headers[] = {"启用", "Excel列", "表头", "用途", "有效语言", "声音", "绑定 / 证据"};
    for (int i = 0; i < ColumnCount; ++i) {
        column_grid_->SetColLabelValue(i, WxUtf8(headers[i]));
    }
    root->Add(column_grid_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    auto* detail_actions=new wxBoxSizer(wxHORIZONTAL);
    detail_actions->Add(new wxStaticText(this,wxID_ANY,WxUtf8("列详情")),1,wxALIGN_CENTER_VERTICAL);
    select_voice_=new wxButton(this,wxID_ANY,WxUtf8("选择声音"));
    select_voice_->Disable();
    select_voice_->Bind(wxEVT_BUTTON,[this](wxCommandEvent&) { SelectVoice(); });
    detail_actions->Add(select_voice_,0);
    root->Add(detail_actions,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,6);
    column_details_=new wxTextCtrl(this,wxID_ANY,wxString{},wxDefaultPosition,FromDIP(wxSize(-1,135)),wxTE_MULTILINE|wxTE_READONLY|wxTE_DONTWRAP);
    root->Add(column_details_,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,6);
    column_grid_->Bind(wxEVT_GRID_SELECT_CELL,[this](wxGridEvent& event) { RefreshColumnDetails(event.GetRow()); event.Skip(); });
    column_grid_->Bind(wxEVT_GRID_CELL_LEFT_DCLICK,[this](wxGridEvent& event) {
        if(event.GetCol()==Voice) { column_grid_->SetGridCursor(event.GetRow(),Voice); SelectVoice(); }
        else event.Skip();
    });
    column_grid_->Bind(wxEVT_CHAR_HOOK,[this](wxKeyEvent& event) {
        if(event.GetKeyCode()==WXK_F2 && column_grid_->GetGridCursorCol()==Voice) SelectVoice();
        else event.Skip();
    });

    status_ = new wxStaticText(this, wxID_ANY,
        runtime_.ConfigLoadMessage().empty() ? WxUtf8("请选择 Excel 并读取 Sheet") : WxUtf8(runtime_.ConfigLoadMessage()));
    root->Add(status_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    browse_button_->Bind(wxEVT_BUTTON, &CorpusMappingPanel::OnBrowse, this);
    load_button_->Bind(wxEVT_BUTTON, &CorpusMappingPanel::OnLoadSheets, this);
    analyze_button_->Bind(wxEVT_BUTTON, &CorpusMappingPanel::OnAnalyze, this);
    build_button_->Bind(wxEVT_BUTTON, &CorpusMappingPanel::OnBuildView, this);
    save_button_->Bind(wxEVT_BUTTON, &CorpusMappingPanel::OnSaveMapping, this);
    select_all_button_->Bind(wxEVT_BUTTON, &CorpusMappingPanel::OnSelectAll, this);
    select_none_button_->Bind(wxEVT_BUTTON, &CorpusMappingPanel::OnSelectNone, this);
    apply_role_button_->Bind(wxEVT_BUTTON, &CorpusMappingPanel::OnApplyRole, this);
    sheet_choice_->Bind(wxEVT_CHOICE, &CorpusMappingPanel::OnSheetChanged, this);
    column_grid_->Bind(wxEVT_GRID_CELL_CHANGED, &CorpusMappingPanel::OnGridCellChanged, this);
    workbook_path_->Bind(wxEVT_TEXT,[this](wxCommandEvent&) { InvalidateAnalysis(); sheets_.clear(); displayed_sheets_.clear(); sheet_choice_->Clear(); });
    header_row_->Bind(wxEVT_SPINCTRL,[this](wxCommandEvent&) { InvalidateAnalysis(); });
    header_row_->Bind(wxEVT_TEXT,[this](wxCommandEvent&) { InvalidateAnalysis(); });
    show_hidden_->Bind(wxEVT_CHECKBOX,[this](wxCommandEvent&) { InvalidateAnalysis(); RefreshSheetChoices(); });
    build_button_->Disable(); save_button_->Disable();

    SetSizer(root);
}

void CorpusMappingPanel::BeginShutdown() {
    if (closing_) return;
    closing_ = true;
    busy_ = true;
    Disable();
}

bool CorpusMappingPanel::CanUseUi() const {
    return !closing_ && !IsBeingDeleted();
}

void CorpusMappingPanel::OnBrowse(wxCommandEvent&) {
    wxFileDialog dialog(this, WxUtf8("选择 Excel"), wxString{}, wxString{}, WxUtf8("Excel workbook (*.xlsx)|*.xlsx"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) {
        workbook_path_->SetValue(dialog.GetPath());
        wxCommandEvent event;
        OnLoadSheets(event);
    }
}

void CorpusMappingPanel::OnLoadSheets(wxCommandEvent&) {
    if (busy_) return;
    const auto path = WorkbookPath();
    const auto last_sheet = runtime_.ConfigSnapshot().last_sheet;
    InvalidateAnalysis();
    const auto revision=input_revision_;
    SetBusy(true, WxUtf8("读取 Sheet 中"));
    runtime_.BackgroundJobs().Submit([this, path, last_sheet, revision](std::stop_token token) {
        try {
            if (token.stop_requested()) return;
            const auto sheets = runtime_.Workbook().SheetMetadata(path);
            if (token.stop_requested()) return;
            CallAfter([this, sheets, last_sheet, revision] {
                if (!CanUseUi()) return;
                if(revision!=input_revision_) { SetBusy(false,WxUtf8("输入已变更，请重新读取")); return; }
                sheets_=sheets;
                RefreshSheetChoices();
                if (!displayed_sheets_.empty()) {
                    const auto selected = last_sheet.empty() ? 0 : sheet_choice_->FindString(WxUtf8(last_sheet));
                    sheet_choice_->SetSelection(selected == wxNOT_FOUND ? 0 : selected);
                    wxCommandEvent event;
                    OnSheetChanged(event);
                }
                SetBusy(false, WxUtf8("已读取 ") + wxString::Format("%zu", sheets.size()) + WxUtf8(" 个 Sheet"));
            });
        } catch (const std::exception& ex) {
            const std::string error = ex.what();
            runtime_.Logger().Error("workbook", error);
            CallAfter([this, error] {
                if (!CanUseUi()) return;
                SetBusy(false, WxUtf8(error));
            });
        }
    });
}

void CorpusMappingPanel::OnSheetChanged(wxCommandEvent&) {
    InvalidateAnalysis();
    if (sheet_choice_->GetSelection() == wxNOT_FOUND) return;
    try {
        const auto identity = WorkbookService::WorkbookIdentity(WorkbookPath());
        const auto saved_header = WorkbookService::FindHeaderRow(runtime_.ConfigSnapshot(), identity, SheetName());
        header_row_->SetValue(static_cast<int>(saved_header.value_or(1)));
    } catch (const std::exception& ex) {
        runtime_.Logger().Error("workbook", ex.what());
        status_->SetLabel(WxUtf8("工作表切换失败：" + std::string(ex.what())));
    }
}

void CorpusMappingPanel::OnAnalyze(wxCommandEvent&) {
    AnalyzeCurrentSheet();
}

void CorpusMappingPanel::AnalyzeCurrentSheet() {
    if (busy_) return;
    if(sheet_choice_->GetSelection()==wxNOT_FOUND) { status_->SetLabel(WxUtf8("请先读取并选择工作表")); return; }
    const auto path = WorkbookPath();
    const auto sheet = SheetName();
    const auto header_row = static_cast<std::size_t>(header_row_->GetValue());
    const auto config = runtime_.ConfigSnapshot();
    InvalidateAnalysis();
    const auto revision=input_revision_;
    SetBusy(true, WxUtf8("分析列中"));
    runtime_.BackgroundJobs().Submit([this, path, sheet, header_row, config, revision](std::stop_token token) {
        try {
            if (token.stop_requested()) return;
            auto analysis = runtime_.Workbook().AnalyzeSheet(path, sheet, header_row, config, token);
            if (token.stop_requested()) return;
            CallAfter([this, revision, analysis = std::move(analysis)]() mutable {
                if (!CanUseUi()) return;
                if(revision!=input_revision_) { SetBusy(false,WxUtf8("输入已变更，请重新分析")); return; }
                analysis_ = std::move(analysis);
                analysis_revision_=revision;
                FillColumnGrid();
                SetBusy(false,
                    WxUtf8("已分析：") +
                    wxString::Format("%zu", analysis_->columns.size()) +
                    WxUtf8(" 列，") +
                    wxString::Format("%zu", analysis_->worksheet.rows.size()) +
                    WxUtf8(" 行"));
            });
        } catch (const std::exception& ex) {
            const std::string error = ex.what();
            runtime_.Logger().Error("workbook", error);
            CallAfter([this, error] {
                if (!CanUseUi()) return;
                SetBusy(false, WxUtf8(error));
            });
        }
    });
}

void CorpusMappingPanel::OnBuildView(wxCommandEvent&) {
    try {
        if (!AnalysisMatchesInput()) {
            status_->SetLabel(WxUtf8("请先分析列"));
            return;
        }
        CorpusViewService service;
        column_grid_->SaveEditControlValue(); column_grid_->DisableCellEditControl();
        auto session = service.CreateSessionFromWorksheet(analysis_->worksheet, SelectedColumnsFromGrid());
        run_panel_->SetSession(std::move(session), [this] {
            if(CanUseUi()) status_->SetLabel(WxUtf8("运行视图已生成"));
        });
    } catch (const std::exception& ex) {
        status_->SetLabel(WxUtf8(ex.what()));
    }
}

void CorpusMappingPanel::OnSaveMapping(wxCommandEvent&) {
    try {
        if (!runtime_.ConfigSaveAllowed()) {
            status_->SetLabel(WxUtf8("当前配置来自未来 schema，拒绝覆盖保存"));
            return;
        }
        if (!AnalysisMatchesInput()) {
            status_->SetLabel(WxUtf8("请先分析列"));
            return;
        }
        column_grid_->SaveEditControlValue(); column_grid_->DisableCellEditControl();
        auto columns = analysis_->columns;
        const auto selected = SelectedColumnsFromGrid();
        for (auto& column : columns) {
            auto it = std::find_if(selected.begin(), selected.end(), [&](const SelectedColumn& item) {
                return item.source_index == column.source_index;
            });
            column.selected = it != selected.end();
            if (it != selected.end()) {
                column.role = it->role;
                const auto fixed_language = GridFixedLanguage(static_cast<int>(&column - columns.data()));
                column.language_selection_mode = fixed_language.empty()
                    ? LanguageSelectionMode::Auto
                    : LanguageSelectionMode::Fixed;
                column.language_code = column.language_selection_mode == LanguageSelectionMode::Fixed
                    ? it->language_code
                    : column.guessed_language;
                column.language_user_overridden = column.language_selection_mode == LanguageSelectionMode::Fixed;
                column.tts_engine_id = it->tts_engine_id;
                column.tts_model_id = it->tts_model_id;
            } else {
                column.role = ColumnRole::Ignore;
            }
        }
        runtime_.SaveConfig([&](AppConfig& config) {
            WorkbookService::UpsertMapping(config, {analysis_->identity, analysis_->sheet_name, analysis_->worksheet.header_row, columns});
            WorkbookService::UpsertHeaderRow(config, analysis_->identity, analysis_->sheet_name, analysis_->worksheet.header_row);
            config.last_workbook = PathToUtf8(analysis_->path);
            config.last_sheet = analysis_->sheet_name;
        },[](AppConfig& config,const AppConfig& before) {
            config.sheet_mappings=before.sheet_mappings;
            config.sheet_header_rows=before.sheet_header_rows;
            config.last_workbook=before.last_workbook;
            config.last_sheet=before.last_sheet;
        });
        status_->SetLabel(WxUtf8("映射已保存"));
    } catch (const std::exception& ex) {
        status_->SetLabel(WxUtf8(ex.what()));
    }
}

void CorpusMappingPanel::FillColumnGrid() {
    std::vector<std::string> model_ids;
    std::vector<std::optional<std::string>> languages;
    if (analysis_) {
        model_ids.reserve(analysis_->columns.size());
        languages.reserve(analysis_->columns.size());
        for (const auto& column : analysis_->columns) {
            model_ids.push_back(column.tts_model_id);
            languages.push_back(column.language_selection_mode == LanguageSelectionMode::Fixed
                ? std::optional<std::string>(column.language_code) : std::nullopt);
        }
    }
    {
        // wxGrid can synchronously select a cell during row insertion/removal.
        rebuilding_grid_ = true;
        wxON_BLOCK_EXIT_SET(rebuilding_grid_, false);
        wxWindowUpdateLocker update_lock(column_grid_);
        row_model_ids_.swap(model_ids);
        row_languages_.swap(languages);
        if (column_grid_->GetNumberRows() > 0) {
            column_grid_->DeleteRows(0, column_grid_->GetNumberRows());
        }
        if (!analysis_) {
            if(column_details_) column_details_->ChangeValue({});
            if(select_voice_) select_voice_->Disable();
            return;
        }

        if (!analysis_->columns.empty()) {
            column_grid_->AppendRows(static_cast<int>(analysis_->columns.size()));
        }
        for (std::size_t r = 0; r < analysis_->columns.size(); ++r) {
            const auto& column = analysis_->columns[r];
            column_grid_->SetCellValue(static_cast<int>(r), Enabled, column.selected ? "1" : "");
            column_grid_->SetCellValue(static_cast<int>(r), ExcelColumn, WxUtf8(column.excel_column));
            column_grid_->SetCellValue(static_cast<int>(r), Header, WxUtf8(column.header));
            column_grid_->SetCellValue(static_cast<int>(r), Role, WxUtf8(RoleText(column.role)));
            column_grid_->SetCellValue(static_cast<int>(r), Language,
                row_languages_[r] ? WxUtf8(*row_languages_[r]) : AutoLanguageLabel(static_cast<int>(r)));
            ConfigureRowEditors(static_cast<int>(r));
            RefreshRowModelStatus(static_cast<int>(r));
            for (int c : {ExcelColumn, Header, Voice, Binding}) {
                column_grid_->SetReadOnly(static_cast<int>(r), c);
            }
        }
        const int widths[] = {45,55,140,65,160,235,170};
        for (int c = 0; c < column_grid_->GetNumberCols(); ++c) {
            column_grid_->SetColSize(c, FromDIP(widths[c]));
        }
    }
    RefreshColumnDetails(column_grid_->GetGridCursorRow());
}

void CorpusMappingPanel::RefreshModelRegistry() {
    model_entries_ = runtime_.ModelScan().entries;
    model_invalid_ = runtime_.ModelScan().invalid;
}

void CorpusMappingPanel::SetBusy(bool busy, const wxString& message) {
    busy_ = busy;
    workbook_path_->Enable(!busy);
    browse_button_->Enable(!busy);
    sheet_choice_->Enable(!busy);
    header_row_->Enable(!busy);
    load_button_->Enable(!busy);
    analyze_button_->Enable(!busy);
    bool playable=false;
    if(!busy && AnalysisMatchesInput() && !analysis_->worksheet.rows.empty()) {
        try { playable=!SelectedColumnsFromGrid().empty(); } catch(const std::exception&) {}
    }
    build_button_->Enable(playable);
    save_button_->Enable(!busy && AnalysisMatchesInput());
    show_hidden_->Enable(!busy);
    select_all_button_->Enable(!busy);
    select_none_button_->Enable(!busy);
    batch_role_->Enable(!busy);
    apply_role_button_->Enable(!busy);
    column_grid_->Enable(!busy);
    select_voice_->Enable(!busy && AnalysisMatchesInput() && column_grid_->GetGridCursorRow()>=0);
    status_->SetLabel(message);
}

void CorpusMappingPanel::ConfigureRowEditors(int row) {
    column_grid_->SetCellRenderer(row, Enabled, new wxGridCellBoolRenderer());
    column_grid_->SetCellEditor(row, Enabled, new wxGridCellBoolEditor());
    column_grid_->SetCellEditor(row, Role, new wxGridCellChoiceEditor(RoleChoices()));
    column_grid_->SetCellEditor(row, Language, new wxGridCellChoiceEditor(LanguageChoices()));
}

wxString CorpusMappingPanel::AutoLanguageLabel(int row) const {
    const auto& code=analysis_->columns.at(row).guessed_language;
    if(code.empty()) return WxUtf8("未识别");
    std::string label=code;
    for(const auto& language:ColumnAnalyzer::Languages()) if(language.code==code) { label=language.name+" / "+code; break; }
    return WxUtf8("自动识别："+label);
}

void CorpusMappingPanel::RefreshColumnDetails(int row) {
    if(rebuilding_grid_ || !column_details_ || !select_voice_) return;
    const bool valid=analysis_ && row>=0 && static_cast<std::size_t>(row)<analysis_->columns.size();
    select_voice_->Enable(valid && !busy_ && !closing_);
    if(!valid) { column_details_->ChangeValue({}); return; }
    const auto& column=analysis_->columns.at(row);
    std::string text=column.excel_column+" / "+column.header+"\n类型："+TypeText(column.suggested_type)+
        "；非空："+std::to_string(column.non_empty_count)+"；来源列索引："+std::to_string(column.source_index)+
        "\n有效语言："+EffectiveLanguageForRow(row)+"\n绑定 ID："+row_model_ids_.at(row);
    const auto found=std::find_if(model_entries_.begin(),model_entries_.end(),[&](const auto& entry) { return entry.id==row_model_ids_.at(row); });
    if(found!=model_entries_.end()) {
        text+="\n"+PathToUtf8(found->root);
        if(found->observation) { text+="\n"+found->observation->Status(); for(const auto& warning:found->observation->warnings) text+="\n"+warning; }
    }
    for(std::size_t i=0;i<column.samples.size() && i<5;++i) text+="\n\n样本 "+std::to_string(i+1)+":\n"+column.samples[i];
    column_details_->ChangeValue(WxUtf8(text));
}

void CorpusMappingPanel::SelectVoice() {
    const int row=column_grid_->GetGridCursorRow();
    if(rebuilding_grid_ || busy_ || closing_ || !AnalysisMatchesInput() || row<0) return;
    column_grid_->SaveEditControlValue(); column_grid_->DisableCellEditControl();
    VoiceSelectionDialog dialog(this,runtime_,model_entries_,EffectiveLanguageForRow(row),row_model_ids_.at(row));
    if(dialog.ShowModal()!=wxID_OK) return;
    row_model_ids_.at(row)=dialog.SelectedId();
    RefreshRowModelStatus(row); RefreshColumnDetails(row);
}

wxArrayString CorpusMappingPanel::LanguageChoices() const {
    std::set<std::string> codes;
    wxArrayString values;
    values.Add(WxUtf8(kAutoLanguageChoice));
    for (const auto& language : ColumnAnalyzer::Languages()) {
        if (!language.code.empty()) codes.insert(language.code);
    }
    for (const auto& entry : model_entries_) {
        if (!entry.config.language_code.empty()) codes.insert(entry.config.language_code);
    }
    for (const auto& code : codes) {
        values.Add(WxUtf8(code));
    }
    return values;
}

std::string CorpusMappingPanel::GridFixedLanguage(int row) const {
    return row_languages_.at(row).value_or(std::string{});
}

std::string CorpusMappingPanel::EffectiveLanguageForRow(int row) const {
    const auto fixed = GridFixedLanguage(row);
    if (!fixed.empty()) return fixed;
    if (analysis_ && row >= 0 && static_cast<std::size_t>(row) < analysis_->columns.size()) {
        return analysis_->columns[static_cast<std::size_t>(row)].guessed_language;
    }
    return {};
}

bool CorpusMappingPanel::IsKnownModelForLanguage(const std::string& model_id, const std::string& language_code) const {
    if (model_id.empty()) return false;
    return std::any_of(model_entries_.begin(), model_entries_.end(), [&](const TtsModelEntry& entry) {
        return entry.id == model_id && entry.config.language_code == language_code;
    });
}

void CorpusMappingPanel::RefreshRowModelStatus(int row) {
    const auto role = RoleFromText(Utf8FromWx(column_grid_->GetCellValue(row, Role)));
    const auto language = EffectiveLanguageForRow(row);
    const auto model_id = row_model_ids_.at(row);
    const auto model=std::find_if(model_entries_.begin(),model_entries_.end(),[&](const auto& entry) { return entry.id==model_id; });
    column_grid_->SetCellValue(row,Voice,model==model_entries_.end() ? WxUtf8(model_id) :
        WxUtf8(model->display_name+" / "+model->config.language_code+" / speaker "+std::to_string(model->config.speaker_id)));
    std::string status;
    if (role != ColumnRole::Play) {
        status = "";
    } else if (model_id.empty()) {
        status = "未绑定";
    } else if (IsKnownModelForLanguage(model_id, language)) {
        status = "已绑定 / "+(model->observation ? model->observation->Status() : std::string("未验证"));
    } else {
        auto invalid = std::find_if(model_invalid_.begin(), model_invalid_.end(), [&](const TtsModelDiagnostic& item) {
            return item.id == model_id;
        });
        status = model!=model_entries_.end() ? "语言不匹配" : (invalid == model_invalid_.end() ? "资源缺失" : "资源不可用");
    }
    column_grid_->SetCellValue(row, Binding, WxUtf8(status));
}

void CorpusMappingPanel::OnGridCellChanged(wxGridEvent& event) {
    if (rebuilding_grid_) { event.Skip(); return; }
    const int row = event.GetRow();
    const int col = event.GetCol();
    if (row >= 0 && col == Role) {
        const auto role = RoleFromText(Utf8FromWx(column_grid_->GetCellValue(row, Role)));
        if (role == ColumnRole::Reference) {
            column_grid_->SetCellValue(row, Enabled, "1");
            for (int r = 0; r < column_grid_->GetNumberRows(); ++r) {
                if (r == row) continue;
                if (RoleFromText(Utf8FromWx(column_grid_->GetCellValue(r, Role))) == ColumnRole::Reference) {
                    column_grid_->SetCellValue(r, Role, WxUtf8("忽略"));
                    column_grid_->SetCellValue(r, Enabled, "");
                    RefreshRowModelStatus(r);
                }
            }
        } else if (role == ColumnRole::Play) {
            column_grid_->SetCellValue(row, Enabled, "1");
        }
    }
    if (row >= 0 && col == Language) {
        const auto value=Utf8FromWx(column_grid_->GetCellValue(row,Language));
        row_languages_.at(row)=value==kAutoLanguageChoice ? std::nullopt : std::optional<std::string>(value);
        if(!row_languages_.at(row)) column_grid_->SetCellValue(row,Language,AutoLanguageLabel(row));
        ConfigureRowEditors(row);
    }
    if (row >= 0) RefreshRowModelStatus(row);
    RefreshColumnDetails(row);
    SetBusy(busy_,status_->GetLabel());
    event.Skip();
}

void CorpusMappingPanel::OnSelectAll(wxCommandEvent&) {
    for (int r = 0; r < column_grid_->GetNumberRows(); ++r) {
        column_grid_->SetCellValue(r, Enabled, "1");
    }
    SetBusy(busy_,status_->GetLabel());
}

void CorpusMappingPanel::OnSelectNone(wxCommandEvent&) {
    for (int r = 0; r < column_grid_->GetNumberRows(); ++r) {
        column_grid_->SetCellValue(r, Enabled, "");
        RefreshRowModelStatus(r);
    }
    SetBusy(busy_,status_->GetLabel());
}

void CorpusMappingPanel::OnApplyRole(wxCommandEvent&) {
    if (!batch_role_ || batch_role_->GetSelection() == wxNOT_FOUND) return;
    const auto role_text = batch_role_->GetStringSelection();
    const auto role = RoleFromText(Utf8FromWx(role_text));
    if (role == ColumnRole::Reference) {
        int selected_count = 0;
        for (int r = 0; r < column_grid_->GetNumberRows(); ++r) {
            if (column_grid_->IsInSelection(r, Enabled) || column_grid_->IsInSelection(r, Role)) {
                ++selected_count;
            }
        }
        if (selected_count != 1) {
            status_->SetLabel(WxUtf8("批量设置参考列时必须且只能选择 1 行"));
            return;
        }
        for(int r=0;r<column_grid_->GetNumberRows();++r) {
            if(!column_grid_->IsInSelection(r,Enabled) && !column_grid_->IsInSelection(r,Role) &&
                RoleFromText(Utf8FromWx(column_grid_->GetCellValue(r,Role)))==ColumnRole::Reference) {
                column_grid_->SetCellValue(r,Role,WxUtf8("忽略"));
                column_grid_->SetCellValue(r,Enabled,"");
                RefreshRowModelStatus(r);
            }
        }
    }
    for (int r = 0; r < column_grid_->GetNumberRows(); ++r) {
        if (column_grid_->IsInSelection(r, Enabled) || column_grid_->IsInSelection(r, Role)) {
            column_grid_->SetCellValue(r, Role, role_text);
            if (role != ColumnRole::Ignore) {
                column_grid_->SetCellValue(r, Enabled, WxUtf8("1"));
            }
            RefreshRowModelStatus(r);
        }
    }
    SetBusy(busy_,status_->GetLabel());
}

std::vector<SelectedColumn> CorpusMappingPanel::SelectedColumnsFromGrid() const {
    if (!analysis_) {
        return {};
    }
    std::vector<SelectedColumn> columns;
    for (int r = 0; r < column_grid_->GetNumberRows(); ++r) {
        const auto selected = Utf8FromWx(column_grid_->GetCellValue(r, Enabled)) == "1";
        if (!selected) {
            continue;
        }
        const auto role = RoleFromText(Utf8FromWx(column_grid_->GetCellValue(r, Role)));
        if (role == ColumnRole::Ignore || role == ColumnRole::Result) {
            continue;
        }
        const auto& profile = analysis_->columns[static_cast<std::size_t>(r)];
        columns.push_back({
            profile.source_index,
            profile.header,
            profile.excel_column,
            role,
            EffectiveLanguageForRow(r),
            "sherpa-vits",
            row_model_ids_.at(r),
        });
    }
    const auto reference_count = std::count_if(columns.begin(), columns.end(), [](const SelectedColumn& column) {
        return column.role == ColumnRole::Reference;
    });
    const auto play_count = std::count_if(columns.begin(), columns.end(), [](const SelectedColumn& column) {
        return column.role == ColumnRole::Play;
    });
    if (reference_count > 1) throw std::runtime_error("参考列最多只能选择 1 列");
    if (play_count == 0) throw std::runtime_error("至少需要选择 1 个播放列");
    return columns;
}

std::filesystem::path CorpusMappingPanel::WorkbookPath() const {
    return PathFromWx(workbook_path_->GetValue());
}

std::string CorpusMappingPanel::SheetName() const {
    if (sheet_choice_->GetSelection() == wxNOT_FOUND) {
        throw std::runtime_error("尚未选择 Sheet");
    }
    return displayed_sheets_.at(static_cast<std::size_t>(sheet_choice_->GetSelection()));
}

void CorpusMappingPanel::InvalidateAnalysis() {
    ++input_revision_; analysis_.reset();
    if(column_grid_) FillColumnGrid();
    if(build_button_) build_button_->Disable();
    if(save_button_) save_button_->Disable();
    if(status_) status_->SetLabel(WxUtf8("输入已变更，请重新分析"));
}
bool CorpusMappingPanel::AnalysisMatchesInput() const {
    if(!analysis_ || analysis_revision_!=input_revision_) return false;
    try { return analysis_->identity==WorkbookService::WorkbookIdentity(WorkbookPath()) &&
        analysis_->sheet_name==SheetName() && analysis_->worksheet.header_row==static_cast<std::size_t>(header_row_->GetValue()); }
    catch(...) { return false; }
}
void CorpusMappingPanel::RefreshSheetChoices() {
    std::string previous;
    if(sheet_choice_->GetSelection()!=wxNOT_FOUND) previous=SheetName();
    sheet_choice_->Clear(); displayed_sheets_.clear();
    for(const auto& sheet:sheets_) {
        if(!show_hidden_->GetValue() && sheet.visibility!=SheetVisibility::Visible) continue;
        auto label=sheet.name;
        if(sheet.visibility!=SheetVisibility::Visible) label+=sheet.visibility==SheetVisibility::Hidden ? " [hidden]" : " [veryHidden]";
        sheet_choice_->Append(WxUtf8(label)); displayed_sheets_.push_back(sheet.name);
    }
    if(!displayed_sheets_.empty()) {
        auto found=std::find(displayed_sheets_.begin(),displayed_sheets_.end(),previous);
        sheet_choice_->SetSelection(found==displayed_sheets_.end()?0:static_cast<int>(found-displayed_sheets_.begin()));
    }
}

} // namespace adayo::ui
