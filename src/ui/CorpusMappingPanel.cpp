#include "ui/CorpusMappingPanel.h"

#include "adapters/excel/OpenXlsxWorkbookReader.h"
#include "services/CorpusViewService.h"
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
        case ColumnRole::Ignore: return "ignore";
        case ColumnRole::Reference: return "reference";
        case ColumnRole::Play: return "play";
        case ColumnRole::Result: return "result";
        case ColumnRole::Index: return "index";
    }
    return "ignore";
}

ColumnRole RoleFromText(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (text == "reference" || text == "ref") return ColumnRole::Reference;
    if (text == "play") return ColumnRole::Play;
    if (text == "result") return ColumnRole::Result;
    return ColumnRole::Ignore;
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
    sheet_row->Add(new wxStaticText(this, wxID_ANY, "Sheet"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    sheet_row->Add(sheet_choice_, 1, wxRIGHT, 12);
    sheet_row->Add(new wxStaticText(this, wxID_ANY, "表头行"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    sheet_row->Add(header_row_, 0, wxRIGHT, 12);
    sheet_row->Add(analyze, 0, wxRIGHT, 6);
    sheet_row->Add(build, 0, wxRIGHT, 6);
    sheet_row->Add(save, 0);
    root->Add(sheet_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    column_grid_ = new wxGrid(this, wxID_ANY);
    column_grid_->CreateGrid(0, 7);
    const char* headers[] = {"选中", "Excel列", "表头", "类型", "语言", "角色", "TTS"};
    for (int i = 0; i < 7; ++i) {
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
        }
        status_->SetLabel(wxString::Format("已读取 %zu 个 Sheet", sheets.size()));
    } catch (const std::exception& ex) {
        status_->SetLabel(FromUtf8(ex.what()));
    }
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
                column.tts_engine_id = it->tts_engine_id;
            } else {
                column.role = ColumnRole::Ignore;
            }
        }
        WorkbookService::UpsertMapping(config_, {analysis_->identity, analysis_->sheet_name, analysis_->worksheet.header_row, columns});
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
        column_grid_->SetCellValue(static_cast<int>(r), 0, column.selected ? "1" : "0");
        column_grid_->SetCellValue(static_cast<int>(r), 1, FromUtf8(column.excel_column));
        column_grid_->SetCellValue(static_cast<int>(r), 2, FromUtf8(column.header));
        column_grid_->SetCellValue(static_cast<int>(r), 3, wxString::FromUTF8(TypeText(column.suggested_type)));
        column_grid_->SetCellValue(static_cast<int>(r), 4, FromUtf8(column.language_code));
        column_grid_->SetCellValue(static_cast<int>(r), 5, wxString::FromUTF8(RoleText(column.role)));
        column_grid_->SetCellValue(static_cast<int>(r), 6, FromUtf8(column.tts_engine_id));
        for (int c : {1, 2, 3}) {
            column_grid_->SetReadOnly(static_cast<int>(r), c);
        }
    }
    column_grid_->AutoSizeColumns(false);
    column_grid_->Thaw();
}

std::vector<SelectedColumn> CorpusMappingPanel::SelectedColumnsFromGrid() const {
    if (!analysis_) {
        return {};
    }
    std::vector<SelectedColumn> columns;
    for (int r = 0; r < column_grid_->GetNumberRows(); ++r) {
        const auto selected = ToUtf8(column_grid_->GetCellValue(r, 0));
        if (selected != "1" && selected != "true" && selected != "yes") {
            continue;
        }
        const auto role = RoleFromText(ToUtf8(column_grid_->GetCellValue(r, 5)));
        if (role == ColumnRole::Ignore || role == ColumnRole::Result) {
            continue;
        }
        const auto& profile = analysis_->columns[static_cast<std::size_t>(r)];
        columns.push_back({
            profile.source_index,
            profile.header,
            profile.excel_column,
            role,
            ToUtf8(column_grid_->GetCellValue(r, 4)),
            ToUtf8(column_grid_->GetCellValue(r, 6)),
        });
    }
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
