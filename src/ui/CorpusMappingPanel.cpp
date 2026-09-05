#include "ui/CorpusMappingPanel.h"

#include "adapters/excel/OpenXlsxWorkbookReader.h"
#include "app/ApplicationRuntime.h"
#include "services/CorpusViewService.h"
#include "services/ModelRegistry.h"
#include "ui/CorpusRunPanel.h"
#include "ui/UiString.h"
#include "platform/UnicodePath.h"

#include <filesystem>
#include <algorithm>
#include <set>
#include <stdexcept>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/grid.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace adayo::ui {
namespace {
const char* kAutoLanguageChoice = "自动 / 未指定";

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
    sheet_row->Add(build_button_, 0, wxRIGHT, 6);
    sheet_row->Add(save_button_, 0, wxRIGHT, 12);
    sheet_row->Add(select_all_button_, 0, wxRIGHT, 6);
    sheet_row->Add(select_none_button_, 0, wxRIGHT, 6);
    sheet_row->Add(batch_role_, 0, wxRIGHT, 6);
    sheet_row->Add(apply_role_button_, 0);
    root->Add(sheet_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    column_grid_ = new wxGrid(this, wxID_ANY);
    column_grid_->CreateGrid(0, 10);
    const char* headers[] = {"启用", "Excel列", "表头", "类型", "非空", "样本", "用途", "语言", "模型/Voice", "模型状态"};
    for (int i = 0; i < 10; ++i) {
        column_grid_->SetColLabelValue(i, WxUtf8(headers[i]));
    }
    root->Add(column_grid_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

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
    analysis_.reset();
    FillColumnGrid();
    SetBusy(true, WxUtf8("读取 Sheet 中"));
    runtime_.BackgroundJobs().Submit([this, path, last_sheet](std::stop_token token) {
        try {
            if (token.stop_requested()) return;
            const auto sheets = runtime_.Workbook().SheetNames(path);
            if (token.stop_requested()) return;
            CallAfter([this, sheets, last_sheet] {
                if (!CanUseUi()) return;
                sheet_choice_->Clear();
                for (const auto& name : sheets) {
                    sheet_choice_->Append(WxUtf8(name));
                }
                if (!sheets.empty()) {
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
    if (sheet_choice_->GetSelection() == wxNOT_FOUND) return;
    const auto identity = WorkbookService::WorkbookIdentity(WorkbookPath());
    const auto saved_header = WorkbookService::FindHeaderRow(runtime_.ConfigSnapshot(), identity, SheetName());
    header_row_->SetValue(static_cast<int>(saved_header.value_or(1)));
}

void CorpusMappingPanel::OnAnalyze(wxCommandEvent&) {
    AnalyzeCurrentSheet();
}

void CorpusMappingPanel::AnalyzeCurrentSheet() {
    if (busy_) return;
    const auto path = WorkbookPath();
    const auto sheet = SheetName();
    const auto header_row = static_cast<std::size_t>(header_row_->GetValue());
    const auto config = runtime_.ConfigSnapshot();
    analysis_.reset();
    FillColumnGrid();
    SetBusy(true, WxUtf8("分析列中"));
    runtime_.BackgroundJobs().Submit([this, path, sheet, header_row, config](std::stop_token token) {
        try {
            if (token.stop_requested()) return;
            auto analysis = runtime_.Workbook().AnalyzeSheet(path, sheet, header_row, config);
            if (token.stop_requested()) return;
            CallAfter([this, analysis = std::move(analysis)]() mutable {
                if (!CanUseUi()) return;
                analysis_ = std::move(analysis);
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
        if (!analysis_) {
            status_->SetLabel(WxUtf8("请先分析列"));
            return;
        }
        CorpusViewService service;
        auto session = service.CreateSession(analysis_->worksheet.rows, SelectedColumnsFromGrid());
        run_panel_->SetSession(std::move(session));
        status_->SetLabel(WxUtf8("运行视图已生成"));
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
        if (!analysis_) {
            status_->SetLabel(WxUtf8("请先分析列"));
            return;
        }
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
        runtime_.UpdateConfig([&](AppConfig& config) {
            WorkbookService::UpsertMapping(config, {analysis_->identity, analysis_->sheet_name, analysis_->worksheet.header_row, columns});
            WorkbookService::UpsertHeaderRow(config, analysis_->identity, analysis_->sheet_name, analysis_->worksheet.header_row);
            config.last_workbook = PathToUtf8(WorkbookPath());
            config.last_sheet = SheetName();
        });
        runtime_.SaveConfig();
        status_->SetLabel(WxUtf8("映射已保存"));
    } catch (const std::exception& ex) {
        status_->SetLabel(WxUtf8(ex.what()));
    }
}

void CorpusMappingPanel::FillColumnGrid() {
    column_grid_->Freeze();
    if (column_grid_->GetNumberRows() > 0) {
        column_grid_->DeleteRows(0, column_grid_->GetNumberRows());
    }
    if (!analysis_) {
        column_grid_->Thaw();
        return;
    }

    column_grid_->AppendRows(static_cast<int>(analysis_->columns.size()));
    for (std::size_t r = 0; r < analysis_->columns.size(); ++r) {
        const auto& column = analysis_->columns[r];
        column_grid_->SetCellValue(static_cast<int>(r), 0, column.selected ? "1" : "");
        column_grid_->SetCellValue(static_cast<int>(r), 1, WxUtf8(column.excel_column));
        column_grid_->SetCellValue(static_cast<int>(r), 2, WxUtf8(column.header));
        column_grid_->SetCellValue(static_cast<int>(r), 3, WxUtf8(TypeText(column.suggested_type)));
        column_grid_->SetCellValue(static_cast<int>(r), 4, wxString::Format("%zu", column.non_empty_count));
        std::string sample_text;
        for (const auto& sample : column.samples) {
            if (!sample_text.empty()) sample_text += " | ";
            sample_text += sample;
        }
        column_grid_->SetCellValue(static_cast<int>(r), 5, WxUtf8(sample_text));
        column_grid_->SetCellValue(static_cast<int>(r), 6, WxUtf8(RoleText(column.role)));
        column_grid_->SetCellValue(static_cast<int>(r), 7,
            column.language_selection_mode == LanguageSelectionMode::Fixed ? WxUtf8(column.language_code) : WxUtf8(kAutoLanguageChoice));
        column_grid_->SetCellValue(static_cast<int>(r), 8, WxUtf8(column.tts_model_id));
        ConfigureRowEditors(static_cast<int>(r));
        RefreshRowModelStatus(static_cast<int>(r));
        for (int c : {1, 2, 3, 4, 5, 9}) {
            column_grid_->SetReadOnly(static_cast<int>(r), c);
        }
    }
    const int widths[] = {55, 70, 180, 90, 70, 260, 90, 140, 260, 130};
    for (int c = 0; c < column_grid_->GetNumberCols(); ++c) {
        column_grid_->SetColSize(c, widths[c]);
    }
    column_grid_->Thaw();
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
    build_button_->Enable(!busy);
    save_button_->Enable(!busy);
    select_all_button_->Enable(!busy);
    select_none_button_->Enable(!busy);
    batch_role_->Enable(!busy);
    apply_role_button_->Enable(!busy);
    column_grid_->Enable(!busy);
    status_->SetLabel(message);
}

void CorpusMappingPanel::ConfigureRowEditors(int row) {
    column_grid_->SetCellRenderer(row, 0, new wxGridCellBoolRenderer());
    column_grid_->SetCellEditor(row, 0, new wxGridCellBoolEditor());
    column_grid_->SetCellEditor(row, 6, new wxGridCellChoiceEditor(RoleChoices()));
    column_grid_->SetCellEditor(row, 7, new wxGridCellChoiceEditor(LanguageChoices()));
    wxArrayString models;
    const auto language = EffectiveLanguageForRow(row);
    const auto current = Utf8FromWx(column_grid_->GetCellValue(row, 8));
    for (const auto& id : ModelIdsForLanguage(language)) {
        models.Add(WxUtf8(id));
    }
    if (!current.empty() && !IsKnownModelForLanguage(current, language)) {
        models.Add(WxUtf8(current));
    }
    column_grid_->SetCellEditor(row, 8, new wxGridCellChoiceEditor(models));
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
    const auto value = Utf8FromWx(column_grid_->GetCellValue(row, 7));
    if (value.empty() || value == kAutoLanguageChoice) return {};
    return value;
}

std::string CorpusMappingPanel::EffectiveLanguageForRow(int row) const {
    const auto fixed = GridFixedLanguage(row);
    if (!fixed.empty()) return fixed;
    if (analysis_ && row >= 0 && static_cast<std::size_t>(row) < analysis_->columns.size()) {
        return analysis_->columns[static_cast<std::size_t>(row)].guessed_language;
    }
    return {};
}

std::vector<std::string> CorpusMappingPanel::ModelIdsForLanguage(const std::string& language_code) const {
    std::vector<std::string> ids;
    if (language_code.empty()) return ids;
    for (const auto& entry : model_entries_) {
        if (entry.config.language_code == language_code) {
            ids.push_back(entry.id);
        }
    }
    return ids;
}

bool CorpusMappingPanel::IsKnownModelForLanguage(const std::string& model_id, const std::string& language_code) const {
    if (model_id.empty()) return false;
    return std::any_of(model_entries_.begin(), model_entries_.end(), [&](const TtsModelEntry& entry) {
        return entry.id == model_id && entry.config.language_code == language_code;
    });
}

void CorpusMappingPanel::RefreshRowModelStatus(int row) {
    const auto role = RoleFromText(Utf8FromWx(column_grid_->GetCellValue(row, 6)));
    const auto language = EffectiveLanguageForRow(row);
    const auto model_id = Utf8FromWx(column_grid_->GetCellValue(row, 8));
    std::string status;
    if (role != ColumnRole::Play) {
        status = "";
    } else if (model_id.empty()) {
        status = "MODEL_REQUIRED";
    } else if (IsKnownModelForLanguage(model_id, language)) {
        status = "OK";
    } else {
        auto invalid = std::find_if(model_invalid_.begin(), model_invalid_.end(), [&](const TtsModelDiagnostic& item) {
            return item.id == model_id;
        });
        status = invalid == model_invalid_.end() ? "MODEL_MISSING" : "MODEL_INVALID";
    }
    column_grid_->SetCellValue(row, 9, WxUtf8(status));
}

void CorpusMappingPanel::OnGridCellChanged(wxGridEvent& event) {
    const int row = event.GetRow();
    const int col = event.GetCol();
    if (row >= 0 && col == 6) {
        const auto role = RoleFromText(Utf8FromWx(column_grid_->GetCellValue(row, 6)));
        if (role == ColumnRole::Reference) {
            column_grid_->SetCellValue(row, 0, "1");
            for (int r = 0; r < column_grid_->GetNumberRows(); ++r) {
                if (r == row) continue;
                if (Utf8FromWx(column_grid_->GetCellValue(r, 0)) == "1" &&
                    RoleFromText(Utf8FromWx(column_grid_->GetCellValue(r, 6))) == ColumnRole::Reference) {
                    column_grid_->SetCellValue(r, 6, WxUtf8("忽略"));
                    column_grid_->SetCellValue(r, 0, "");
                }
            }
        } else if (role == ColumnRole::Play) {
            column_grid_->SetCellValue(row, 0, "1");
        }
    }
    if (row >= 0 && col == 7) {
        column_grid_->SetCellValue(row, 8, "");
        ConfigureRowEditors(row);
    }
    if (row >= 0) RefreshRowModelStatus(row);
    event.Skip();
}

void CorpusMappingPanel::OnSelectAll(wxCommandEvent&) {
    for (int r = 0; r < column_grid_->GetNumberRows(); ++r) {
        column_grid_->SetCellValue(r, 0, "1");
    }
}

void CorpusMappingPanel::OnSelectNone(wxCommandEvent&) {
    for (int r = 0; r < column_grid_->GetNumberRows(); ++r) {
        column_grid_->SetCellValue(r, 0, "");
        RefreshRowModelStatus(r);
    }
}

void CorpusMappingPanel::OnApplyRole(wxCommandEvent&) {
    if (!batch_role_ || batch_role_->GetSelection() == wxNOT_FOUND) return;
    const auto role_text = batch_role_->GetStringSelection();
    const auto role = RoleFromText(Utf8FromWx(role_text));
    if (role == ColumnRole::Reference) {
        int selected_count = 0;
        for (int r = 0; r < column_grid_->GetNumberRows(); ++r) {
            if (column_grid_->IsInSelection(r, 0) || column_grid_->IsInSelection(r, 6)) {
                ++selected_count;
            }
        }
        if (selected_count != 1) {
            status_->SetLabel(WxUtf8("批量设置参考列时必须且只能选择 1 行"));
            return;
        }
    }
    for (int r = 0; r < column_grid_->GetNumberRows(); ++r) {
        if (column_grid_->IsInSelection(r, 0) || column_grid_->IsInSelection(r, 6)) {
            column_grid_->SetCellValue(r, 6, role_text);
            if (role != ColumnRole::Ignore) {
                column_grid_->SetCellValue(r, 0, WxUtf8("1"));
            }
            RefreshRowModelStatus(r);
        }
    }
}

std::vector<SelectedColumn> CorpusMappingPanel::SelectedColumnsFromGrid() const {
    if (!analysis_) {
        return {};
    }
    std::vector<SelectedColumn> columns;
    for (int r = 0; r < column_grid_->GetNumberRows(); ++r) {
        const auto selected = Utf8FromWx(column_grid_->GetCellValue(r, 0)) == "1";
        if (!selected) {
            continue;
        }
        const auto role = RoleFromText(Utf8FromWx(column_grid_->GetCellValue(r, 6)));
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
            Utf8FromWx(column_grid_->GetCellValue(r, 8)),
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
    return Utf8FromWx(sheet_choice_->GetStringSelection());
}

} // namespace adayo::ui
