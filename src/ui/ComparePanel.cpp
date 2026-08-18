#include "ui/ComparePanel.h"

#include "adapters/excel/LibXlsxWriterExporter.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/filedlg.h>
#include <wx/grid.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace adayo::ui {
namespace {
wxString FromUtf8(const std::string& text) {
    return wxString::FromUTF8(text);
}

std::string ToUtf8(const wxString& text) {
    return text.ToUTF8().data() ? std::string(text.ToUTF8().data()) : std::string{};
}

const char* StatusText(CompareStatus status) {
    switch (status) {
        case CompareStatus::Ok: return "OK";
        case CompareStatus::Ng: return "NG";
        case CompareStatus::Missing: return "MISSING";
        case CompareStatus::Extra: return "EXTRA";
    }
    return "NG";
}
} // namespace

ComparePanel::ComparePanel(wxWindow* parent) : wxPanel(parent) {
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto add_file_row = [&](const char* label, wxTextCtrl*& target, auto handler) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(this, wxID_ANY, wxString::FromUTF8(label)), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        target = new wxTextCtrl(this, wxID_ANY);
        auto* button = new wxButton(this, wxID_ANY, "打开");
        button->Bind(wxEVT_BUTTON, handler, this);
        row->Add(target, 1, wxRIGHT, 8);
        row->Add(button, 0);
        root->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);
    };
    add_file_row("正式文本:", reference_path_, &ComparePanel::OnOpenReference);
    add_file_row("机器文本:", actual_path_, &ComparePanel::OnOpenActual);

    auto* thresholds = new wxBoxSizer(wxHORIZONTAL);
    align_threshold_ = new wxSpinCtrlDouble(this, wxID_ANY); align_threshold_->SetRange(0, 100); align_threshold_->SetValue(80); align_threshold_->SetIncrement(1);
    pass_threshold_ = new wxSpinCtrlDouble(this, wxID_ANY); pass_threshold_->SetRange(0, 100); pass_threshold_->SetValue(100); pass_threshold_->SetIncrement(1);
    ignore_punctuation_ = new wxCheckBox(this, wxID_ANY, "忽略标点");
    ignore_punctuation_->SetValue(true);
    auto* compare = new wxButton(this, wxID_ANY, "自动对齐");
    auto* export_excel = new wxButton(this, wxID_ANY, "导出 Excel");
    compare->Bind(wxEVT_BUTTON, &ComparePanel::OnCompare, this);
    export_excel->Bind(wxEVT_BUTTON, &ComparePanel::OnExport, this);
    thresholds->Add(new wxStaticText(this, wxID_ANY, "对齐阈值"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    thresholds->Add(align_threshold_, 0, wxRIGHT, 18);
    thresholds->Add(new wxStaticText(this, wxID_ANY, "OK阈值"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    thresholds->Add(pass_threshold_, 0, wxRIGHT, 18);
    thresholds->Add(ignore_punctuation_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 18);
    thresholds->Add(compare, 0, wxRIGHT, 8);
    thresholds->Add(export_excel, 0);
    root->Add(thresholds, 0, wxALL, 10);

    result_grid_ = new wxGrid(this, wxID_ANY);
    result_grid_->CreateGrid(0, 6);
    const char* headers[] = {"正式序号", "机器序号", "正式文本", "机器文本", "相似度", "结果"};
    for (int i = 0; i < 6; ++i) {
        result_grid_->SetColLabelValue(i, wxString::FromUTF8(headers[i]));
    }
    result_grid_->EnableEditing(false);
    root->Add(result_grid_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    SetSizer(root);
}

void ComparePanel::OnOpenReference(wxCommandEvent&) {
    LoadFileInto(reference_path_);
}

void ComparePanel::OnOpenActual(wxCommandEvent&) {
    LoadFileInto(actual_path_);
}

void ComparePanel::OnCompare(wxCommandEvent&) {
    try {
        CompareOptions options;
        options.normalizer.ignore_punctuation = ignore_punctuation_->GetValue();
        options.normalizer.case_fold = true;
        options.normalizer.unicode_nfkc = true;
        options.alignment.alignment_threshold = align_threshold_->GetValue();
        options.alignment.anchor_threshold = 95.0;
        options.pass_threshold = pass_threshold_->GetValue();

        CompareService service;
        rows_ = service.Compare(ReadLines(reference_path_->GetValue()), ReadLines(actual_path_->GetValue()), options);
        RefreshGrid();
    } catch (const std::exception& ex) {
        rows_.clear();
        RefreshGrid();
        result_grid_->SetColLabelValue(0, FromUtf8(ex.what()));
    }
}

void ComparePanel::OnExport(wxCommandEvent&) {
    if (rows_.empty()) {
        wxCommandEvent event;
        OnCompare(event);
    }
    if (rows_.empty()) return;
    wxFileDialog dialog(this, "导出对比 Excel", "", "compare.xlsx", "Excel workbook (*.xlsx)|*.xlsx", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() == wxID_OK) {
        try {
            LibXlsxWriterExporter exporter;
            exporter.ExportComparison(rows_, std::filesystem::path(dialog.GetPath().ToStdWstring()));
        } catch (const std::exception& ex) {
            result_grid_->SetColLabelValue(0, FromUtf8(ex.what()));
        }
    }
}

void ComparePanel::LoadFileInto(wxTextCtrl* target) {
    wxFileDialog dialog(this, "选择文本文件", "", "", "Text files (*.txt)|*.txt|All files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) {
        target->SetValue(dialog.GetPath());
    }
}

void ComparePanel::RefreshGrid() {
    result_grid_->Freeze();
    if (result_grid_->GetNumberRows() > 0) {
        result_grid_->DeleteRows(0, result_grid_->GetNumberRows());
    }
    if (!rows_.empty()) {
        result_grid_->AppendRows(static_cast<int>(rows_.size()));
    }
    for (std::size_t r = 0; r < rows_.size(); ++r) {
        const auto& row = rows_[r];
        result_grid_->SetCellValue(static_cast<int>(r), 0, row.reference_index ? wxString::Format("%zu", *row.reference_index + 1) : wxString{});
        result_grid_->SetCellValue(static_cast<int>(r), 1, row.actual_index ? wxString::Format("%zu", *row.actual_index + 1) : wxString{});
        result_grid_->SetCellValue(static_cast<int>(r), 2, FromUtf8(row.reference_text));
        result_grid_->SetCellValue(static_cast<int>(r), 3, FromUtf8(row.actual_text));
        result_grid_->SetCellValue(static_cast<int>(r), 4, wxString::Format("%.2f", row.similarity));
        result_grid_->SetCellValue(static_cast<int>(r), 5, wxString::FromUTF8(StatusText(row.status)));
    }
    result_grid_->AutoSizeColumns(false);
    result_grid_->Thaw();
}

std::vector<std::string> ComparePanel::ReadLines(const wxString& path) {
    std::ifstream input(std::filesystem::path(path.ToStdWstring()), std::ios::binary);
    if (!input) {
        throw std::runtime_error("无法读取文本文件: " + ToUtf8(path));
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}
} // namespace adayo::ui
