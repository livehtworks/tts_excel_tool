#include "ui/CorpusMappingPanel.h"

#include "adapters/excel/OpenXlsxWorkbookReader.h"
#include "services/CorpusViewService.h"
#include "services/ModelRegistry.h"
#include "ui/CorpusRunPanel.h"

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <stdexcept>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/grid.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/textctrl.h>

namespace adayo::ui {
namespace {
wxString FromUtf8(const std::string& text) {
    return wxString::FromUTF8(text);
}

std::string ToUtf8(const wxString& text) {
    return text.ToUTF8().data() ? std::string(text.ToUTF8().data()) : std::string{};
}

std::filesystem::path ConfigPath() {
    const auto exe = std::filesystem::path(ToUtf8(wxStandardPaths::Get().GetExecutablePath()));
    return exe.parent_path() / "config" / "config.json";
}

std::filesystem::path ModelsPath() {
    const auto exe = std::filesystem::path(ToUtf8(wxStandardPaths::Get().GetExecutablePath()));
    return exe.parent_path() / "models" / "sherpa";
}

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
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (text == "参考") return ColumnRole::Reference;
    if (text == "播放") return ColumnRole::Play;
    if (text == "结果") return ColumnRole::Result;
    if (text == "序号") return ColumnRole::Index;
    if (text == "忽略") return ColumnRole::Ignore;
    if (text == "reference" || text == "ref") return ColumnRole::Reference;
    if (text == "play") return ColumnRole::Play;
    if (text == "result") return ColumnRole::Result;
    return ColumnRole::Ignore;
}

wxArrayString RoleChoices() {
    wxArrayString values;
    values.Add("忽略");
    values.Add("参考");
    values.Add("播放");
    return values;
}

wxArrayString LanguageChoices() {
    wxArrayString values;
    for (const auto& language : ColumnAnalyzer::Languages()) {
        values.Add(FromUtf8(language.code));
    }
    return values;
}
} // namespace

CorpusMappingPanel::CorpusMappingPanel(wxWindow* parent, CorpusRunPanel* run_panel)
    : wxPanel(parent),
      run_panel_(run_panel),
      workbook_service_(std::make_unique<WorkbookService>(std::make_unique<OpenXlsxWorkbookReader>())),
      config_store_(ConfigPath()) {

    try {
        config_ = config_store_.Load();
    } catch (...) {
        config_ = {};
    }
    LoadModelRegistry();

    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* file_row = new wxBoxSizer(wxHORIZONTAL);
    workbook_path_ = new wxTextCtrl(this, wxID_ANY, FromUtf8(config_.last_workbook));
    auto* browse = new wxButton(this, wxID_ANY, "浏览");
    auto* load = new wxButton(this, wxID_ANY, "读取 Sheet");
    file_row->Add(new wxStaticText(this, wxID_ANY, "Excel"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    file_row->Add(workbook_path_, 1, wxRIGHT, 6);
    file_row->Add(browse, 0, wxRIGHT, 6);
    file_row->Add(load, 0);
    root->Add(file_row, 0, wxEXPAND | wxALL, 6);

    auto* sheet_row = new wxBoxSizer(wxHORIZONTAL);
    sheet_choice_ = new wxChoice(this, wxID_ANY);
    header_row_ = new wxSpinCtrl(this, wxID_ANY);
    header_row_->SetRange(1, 1000);
    header_row_->SetValue(1);
    auto* analyze = new wxButton(this, wxID_ANY, "分析列");
    auto* build = new wxButton(this, wxID_ANY, "生成运行视图");
    auto* save = new wxButton(this, wxID_ANY, "保存映射");
    auto* select_all = new wxButton(this, wxID_ANY, "全选");
    auto* select_none = new wxButton(this, wxID_ANY, "全不选");
    batch_role_ = new wxChoice(this, wxID_ANY);
    batch_role_->Append("忽略");
    batch_role_->Append("参考");
    batch_role_->Append("播放");
    batch_role_->SetSelection(0);
    auto* apply_role = new wxButton(this, wxID_ANY, "批量用途");
    sheet_row->Add(new wxStaticText(this, wxID_ANY, "Sheet"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    sheet_row->Add(sheet_choice_, 1, wxRIGHT, 12);
    sheet_row->Add(new wxStaticText(this, wxID_ANY, "表头行"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    sheet_row->Add(header_row_, 0, wxRIGHT, 12);
    sheet_row->Add(analyze, 0, wxRIGHT, 6);
    sheet_row->Add(build, 0, wxRIGHT, 6);
    sheet_row->Add(save, 0, wxRIGHT, 12);
    sheet_row->Add(select_all, 0, wxRIGHT, 6);
    sheet_row->Add(select_none, 0, wxRIGHT, 6);
    sheet_row->Add(batch_role_, 0, wxRIGHT, 6);
    sheet_row->Add(apply_role, 0);
    root->Add(sheet_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    column_grid_ = new wxGrid(this, wxID_ANY);
    column_grid_->CreateGrid(0, 10);
    const char* headers[] = {"启用", "Excel列", "表头", "类型", "非空", "样本", "用途", "语言", "模型/Voice", "模型状态"};
    for (int i = 0; i < 10; ++i) {
        column_grid_->SetColLabelValue(i, wxString::FromUTF8(headers[i]));
    }
    root->Add(column_grid_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    status_ = new wxStaticText(this, wxID_ANY, "请选择 Excel 并读取 Sheet");
    root->Add(status_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    browse->Bind(wxEVT_BUTTON, &CorpusMappingPanel::OnBrowse, this);
    load->Bind(wxEVT_BUTTON, &CorpusMappingPanel::OnLoadSheets, this);
    analyze->Bind(wxEVT_BUTTON, &CorpusMappingPanel::OnAnalyze, this);
    build->Bind(wxEVT_BUTTON, &CorpusMappingPanel::OnBuildView, this);
    save->Bind(wxEVT_BUTTON, &CorpusMappingPanel::OnSaveMapping, this);
    select_all->Bind(wxEVT_BUTTON, &CorpusMappingPanel::OnSelectAll, this);
    select_none->Bind(wxEVT_BUTTON, &CorpusMappingPanel::OnSelectNone, this);
    apply_role->Bind(wxEVT_BUTTON, &CorpusMappingPanel::OnApplyRole, this);
    sheet_choice_->Bind(wxEVT_CHOICE, &CorpusMappingPanel::OnSheetChanged, this);
    column_grid_->Bind(wxEVT_GRID_CELL_CHANGED, &CorpusMappingPanel::OnGridCellChanged, this);

    SetSizer(root);
}

void CorpusMappingPanel::OnBrowse(wxCommandEvent&) {
    wxFileDialog dialog(this, "选择 Excel", "", "", "Excel workbook (*.xlsx)|*.xlsx", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) {
        workbook_path_->SetValue(dialog.GetPath());
        wxCommandEvent event;
        OnLoadSheets(event);
    }
}

void CorpusMappingPanel::OnLoadSheets(wxCommandEvent&) {
    try {
        sheet_choice_->Clear();
        const auto sheets = workbook_service_->SheetNames(WorkbookPath());
        for (const auto& name : sheets) {
            sheet_choice_->Append(FromUtf8(name));
        }
        if (!sheets.empty()) {
            const auto selected = config_.last_sheet.empty() ? 0 : sheet_choice_->FindString(FromUtf8(config_.last_sheet));
            sheet_choice_->SetSelection(selected == wxNOT_FOUND ? 0 : selected);
            wxCommandEvent event;
            OnSheetChanged(event);
        }
        status_->SetLabel(wxString::Format("已读取 %zu 个 Sheet", sheets.size()));
    } catch (const std::exception& ex) {
        status_->SetLabel(FromUtf8(ex.what()));
    }
}

void CorpusMappingPanel::OnSheetChanged(wxCommandEvent&) {
    if (sheet_choice_->GetSelection() == wxNOT_FOUND) return;
    const auto identity = WorkbookService::WorkbookIdentity(WorkbookPath());
    const auto saved_header = WorkbookService::FindHeaderRow(config_, identity, SheetName());
    header_row_->SetValue(static_cast<int>(saved_header.value_or(1)));
}

void CorpusMappingPanel::OnAnalyze(wxCommandEvent&) {
    AnalyzeCurrentSheet();
}

void CorpusMappingPanel::AnalyzeCurrentSheet() {
    try {
        analysis_ = workbook_service_->AnalyzeSheet(WorkbookPath(), SheetName(), static_cast<std::size_t>(header_row_->GetValue()), config_);
        FillColumnGrid();
        status_->SetLabel(wxString::Format("已分析：%zu 列，%zu 行", analysis_->columns.size(), analysis_->worksheet.rows.size()));
    } catch (const std::exception& ex) {
        status_->SetLabel(FromUtf8(ex.what()));
    }
}

void CorpusMappingPanel::OnBuildView(wxCommandEvent&) {
    try {
        if (!analysis_) AnalyzeCurrentSheet();
        if (!analysis_) return;
        CorpusViewService service;
        auto session = service.CreateSession(analysis_->worksheet.rows, SelectedColumnsFromGrid());
        run_panel_->SetSession(std::move(session));
        status_->SetLabel("运行视图已生成");
    } catch (const std::exception& ex) {
        status_->SetLabel(FromUtf8(ex.what()));
    }
}

void CorpusMappingPanel::OnSaveMapping(wxCommandEvent&) {
    try {
        if (!analysis_) AnalyzeCurrentSheet();
        if (!analysis_) return;
        auto columns = analysis_->columns;
        const auto selected = SelectedColumnsFromGrid();
        for (auto& column : columns) {
            auto it = std::find_if(selected.begin(), selected.end(), [&](const SelectedColumn& item) {
                return item.source_index == column.source_index;
            });
            column.selected = it != selected.end();
            if (it != selected.end()) {
                column.role = it->role;
                column.language_code = it->language_code;
                column.language_user_overridden = column.language_code != column.guessed_language;
                column.tts_engine_id = it->tts_engine_id;
                column.tts_model_id = it->tts_model_id;
            } else {
                column.role = ColumnRole::Ignore;
            }
        }
        WorkbookService::UpsertMapping(config_, {analysis_->identity, analysis_->sheet_name, analysis_->worksheet.header_row, columns});
        WorkbookService::UpsertHeaderRow(config_, analysis_->identity, analysis_->sheet_name, analysis_->worksheet.header_row);
        config_.last_workbook = WorkbookPath();
        config_.last_sheet = SheetName();
        config_store_.Save(config_);
        status_->SetLabel("映射已保存");
    } catch (const std::exception& ex) {
        status_->SetLabel(FromUtf8(ex.what()));
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
        column_grid_->SetCellValue(static_cast<int>(r), 1, FromUtf8(column.excel_column));
        column_grid_->SetCellValue(static_cast<int>(r), 2, FromUtf8(column.header));
        column_grid_->SetCellValue(static_cast<int>(r), 3, wxString::FromUTF8(TypeText(column.suggested_type)));
        column_grid_->SetCellValue(static_cast<int>(r), 4, wxString::Format("%zu", column.non_empty_count));
        std::string sample_text;
        for (const auto& sample : column.samples) {
            if (!sample_text.empty()) sample_text += " | ";
            sample_text += sample;
        }
        column_grid_->SetCellValue(static_cast<int>(r), 5, FromUtf8(sample_text));
        column_grid_->SetCellValue(static_cast<int>(r), 6, wxString::FromUTF8(RoleText(column.role)));
        column_grid_->SetCellValue(static_cast<int>(r), 7, FromUtf8(column.language_code));
        column_grid_->SetCellValue(static_cast<int>(r), 8, FromUtf8(column.tts_model_id));
        ConfigureRowEditors(static_cast<int>(r));
        RefreshRowModelStatus(static_cast<int>(r));
        for (int c : {1, 2, 3, 4, 5, 9}) {
            column_grid_->SetReadOnly(static_cast<int>(r), c);
        }
    }
    column_grid_->AutoSizeColumns(false);
    column_grid_->Thaw();
}

void CorpusMappingPanel::LoadModelRegistry() {
    ModelRegistry registry(ModelsPath());
    const auto scan = registry.ScanSherpaModelsWithDiagnostics();
    model_entries_ = scan.entries;
    model_invalid_ = scan.invalid;
}

void CorpusMappingPanel::ConfigureRowEditors(int row) {
    column_grid_->SetCellRenderer(row, 0, new wxGridCellBoolRenderer());
    column_grid_->SetCellEditor(row, 0, new wxGridCellBoolEditor());
    column_grid_->SetCellEditor(row, 6, new wxGridCellChoiceEditor(RoleChoices()));
    column_grid_->SetCellEditor(row, 7, new wxGridCellChoiceEditor(LanguageChoices()));
    wxArrayString models;
    const auto language = ToUtf8(column_grid_->GetCellValue(row, 7));
    const auto current = ToUtf8(column_grid_->GetCellValue(row, 8));
    for (const auto& id : ModelIdsForLanguage(language)) {
        models.Add(FromUtf8(id));
    }
    if (!current.empty() && !IsKnownModelForLanguage(current, language)) {
        models.Add(FromUtf8(current));
    }
    column_grid_->SetCellEditor(row, 8, new wxGridCellChoiceEditor(models));
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
    const auto role = RoleFromText(ToUtf8(column_grid_->GetCellValue(row, 6)));
    const auto language = ToUtf8(column_grid_->GetCellValue(row, 7));
    const auto model_id = ToUtf8(column_grid_->GetCellValue(row, 8));
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
    column_grid_->SetCellValue(row, 9, FromUtf8(status));
}

void CorpusMappingPanel::OnGridCellChanged(wxGridEvent& event) {
    const int row = event.GetRow();
    const int col = event.GetCol();
    if (row >= 0 && (col == 6 || col == 0)) {
        const auto role = RoleFromText(ToUtf8(column_grid_->GetCellValue(row, 6)));
        if (role == ColumnRole::Reference) {
            column_grid_->SetCellValue(row, 0, "1");
            for (int r = 0; r < column_grid_->GetNumberRows(); ++r) {
                if (r == row) continue;
                if (RoleFromText(ToUtf8(column_grid_->GetCellValue(r, 6))) == ColumnRole::Reference) {
                    column_grid_->SetCellValue(r, 6, "忽略");
                    column_grid_->SetCellValue(r, 0, "");
                }
            }
        } else if (role == ColumnRole::Play) {
            column_grid_->SetCellValue(row, 0, "1");
        } else if (ToUtf8(column_grid_->GetCellValue(row, 0)) != "1") {
            column_grid_->SetCellValue(row, 6, "忽略");
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
        column_grid_->SetCellValue(r, 6, "忽略");
        RefreshRowModelStatus(r);
    }
}

void CorpusMappingPanel::OnApplyRole(wxCommandEvent&) {
    if (!batch_role_ || batch_role_->GetSelection() == wxNOT_FOUND) return;
    const auto role_text = batch_role_->GetStringSelection();
    for (int r = 0; r < column_grid_->GetNumberRows(); ++r) {
        if (column_grid_->IsInSelection(r, 0) || column_grid_->IsInSelection(r, 6)) {
            column_grid_->SetCellValue(r, 6, role_text);
            column_grid_->SetCellValue(r, 0, role_text == "忽略" ? "" : "1");
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
        const auto selected = column_grid_->GetCellValue(r, 0) == "1";
        if (!selected) {
            continue;
        }
        const auto role = RoleFromText(ToUtf8(column_grid_->GetCellValue(r, 6)));
        if (role == ColumnRole::Ignore || role == ColumnRole::Result) {
            continue;
        }
        const auto& profile = analysis_->columns[static_cast<std::size_t>(r)];
        columns.push_back({
            profile.source_index,
            profile.header,
            profile.excel_column,
            role,
            ToUtf8(column_grid_->GetCellValue(r, 7)),
            "sherpa-vits",
            ToUtf8(column_grid_->GetCellValue(r, 8)),
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

std::string CorpusMappingPanel::WorkbookPath() const {
    return ToUtf8(workbook_path_->GetValue());
}

std::string CorpusMappingPanel::SheetName() const {
    if (sheet_choice_->GetSelection() == wxNOT_FOUND) {
        throw std::runtime_error("尚未选择 Sheet");
    }
    return ToUtf8(sheet_choice_->GetStringSelection());
}

} // namespace adayo::ui
