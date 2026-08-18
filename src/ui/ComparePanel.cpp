#include "ui/ComparePanel.h"

#include "adapters/excel/LibXlsxWriterExporter.h"
#include "adapters/text/TextFileImporter.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/filedlg.h>
#include <wx/grid.h>
#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

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

std::string PathUtf8(const std::filesystem::path& path) {
    const auto u8 = path.u8string();
    return {u8.begin(), u8.end()};
}

std::string DefaultLabel(const std::filesystem::path& path, std::size_t index) {
    const auto stem = path.stem().u8string();
    if (!stem.empty()) return {stem.begin(), stem.end()};
    return "Group " + std::to_string(index + 1);
}
} // namespace

ComparePanel::ComparePanel(wxWindow* parent) : wxPanel(parent) {
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* input = new wxBoxSizer(wxVERTICAL);
    auto add_text_row = [&](const char* label, wxTextCtrl*& target, bool browse, auto handler) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(this, wxID_ANY, wxString::FromUTF8(label)), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        target = new wxTextCtrl(this, wxID_ANY);
        row->Add(target, 1, wxRIGHT, 8);
        if (browse) {
            auto* button = new wxButton(this, wxID_ANY, "打开");
            button->Bind(wxEVT_BUTTON, handler, this);
            row->Add(button, 0);
        }
        input->Add(row, 0, wxEXPAND | wxBOTTOM, 6);
    };
    add_text_row("语言组:", language_label_, false, &ComparePanel::OnOpenReference);
    add_text_row("正式文本:", reference_path_, true, &ComparePanel::OnOpenReference);
    add_text_row("机器文本:", actual_path_, true, &ComparePanel::OnOpenActual);
    add_text_row("固定分隔:", delimiter_, false, &ComparePanel::OnOpenReference);
    root->Add(input, 0, wxEXPAND | wxALL, 10);

    auto* group_bar = new wxBoxSizer(wxHORIZONTAL);
    group_list_ = new wxListBox(this, wxID_ANY);
    add_group_ = new wxButton(this, wxID_ANY, "加入组");
    remove_group_ = new wxButton(this, wxID_ANY, "移除组");
    add_group_->Bind(wxEVT_BUTTON, &ComparePanel::OnAddGroup, this);
    remove_group_->Bind(wxEVT_BUTTON, &ComparePanel::OnRemoveGroup, this);
    auto* group_buttons = new wxBoxSizer(wxVERTICAL);
    group_buttons->Add(add_group_, 0, wxEXPAND | wxBOTTOM, 6);
    group_buttons->Add(remove_group_, 0, wxEXPAND);
    group_bar->Add(group_list_, 1, wxRIGHT, 8);
    group_bar->Add(group_buttons, 0);
    root->Add(group_bar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    auto* thresholds = new wxBoxSizer(wxHORIZONTAL);
    align_threshold_ = new wxSpinCtrlDouble(this, wxID_ANY);
    align_threshold_->SetRange(0, 100);
    align_threshold_->SetValue(80);
    align_threshold_->SetIncrement(1);
    pass_threshold_ = new wxSpinCtrlDouble(this, wxID_ANY);
    pass_threshold_->SetRange(0, 100);
    pass_threshold_->SetValue(100);
    pass_threshold_->SetIncrement(1);
    ignore_punctuation_ = new wxCheckBox(this, wxID_ANY, "忽略标点");
    ignore_punctuation_->SetValue(true);
    compare_button_ = new wxButton(this, wxID_ANY, "自动对齐");
    export_button_ = new wxButton(this, wxID_ANY, "导出 Excel");
    status_ = new wxStaticText(this, wxID_ANY, "就绪");
    compare_button_->Bind(wxEVT_BUTTON, &ComparePanel::OnCompare, this);
    export_button_->Bind(wxEVT_BUTTON, &ComparePanel::OnExport, this);
    thresholds->Add(new wxStaticText(this, wxID_ANY, "对齐阈值"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    thresholds->Add(align_threshold_, 0, wxRIGHT, 18);
    thresholds->Add(new wxStaticText(this, wxID_ANY, "OK阈值"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    thresholds->Add(pass_threshold_, 0, wxRIGHT, 18);
    thresholds->Add(ignore_punctuation_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 18);
    thresholds->Add(compare_button_, 0, wxRIGHT, 8);
    thresholds->Add(export_button_, 0, wxRIGHT, 12);
    thresholds->Add(status_, 1, wxALIGN_CENTER_VERTICAL);
    root->Add(thresholds, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

    result_grid_ = new wxGrid(this, wxID_ANY);
    result_grid_->CreateGrid(0, 0);
    result_grid_->EnableEditing(false);
    root->Add(result_grid_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    SetSizer(root);
    RefreshGroups();
    RefreshGrid();
}

void ComparePanel::OnOpenReference(wxCommandEvent&) {
    LoadFileInto(reference_path_);
}

void ComparePanel::OnOpenActual(wxCommandEvent&) {
    LoadFileInto(actual_path_);
}

void ComparePanel::OnAddGroup(wxCommandEvent&) {
    const auto reference = std::filesystem::path(reference_path_->GetValue().ToStdWstring());
    const auto actual = std::filesystem::path(actual_path_->GetValue().ToStdWstring());
    if (reference.empty() || actual.empty()) {
        SetStatus("请选择正式文本和机器文本");
        return;
    }
    CompareInputGroup group;
    group.label = ToUtf8(language_label_->GetValue());
    if (group.label.empty()) group.label = DefaultLabel(reference, input_groups_.size());
    group.reference_path = reference;
    group.actual_path = actual;
    input_groups_.push_back(std::move(group));
    RefreshGroups();
}

void ComparePanel::OnRemoveGroup(wxCommandEvent&) {
    const int selection = group_list_->GetSelection();
    if (selection != wxNOT_FOUND && static_cast<std::size_t>(selection) < input_groups_.size()) {
        input_groups_.erase(input_groups_.begin() + selection);
        RefreshGroups();
    }
}

void ComparePanel::OnCompare(wxCommandEvent&) {
    if (busy_) return;
    const auto groups = CurrentInputGroups();
    const auto options = CurrentOptions();
    const auto delimiter = CurrentDelimiter();
    if (groups.empty()) {
        SetStatus("没有可对比的语言组");
        return;
    }
    SetBusy(true, "对比中");
    worker_.Submit([this, groups, options, delimiter] {
        try {
            CompareService service;
            std::vector<CompareReportGroup> reports;
            reports.reserve(groups.size());
            TextImportOptions import_options;
            import_options.delimiter = delimiter;
            for (const auto& group : groups) {
                const auto reference = TextFileImporter::ReadUtf8Records(group.reference_path, import_options);
                const auto actual = TextFileImporter::ReadUtf8Records(group.actual_path, import_options);
                reports.push_back({group.label, service.Compare(reference, actual, options)});
            }
            CallAfter([this, reports = std::move(reports)]() mutable {
                report_groups_ = std::move(reports);
                RefreshGrid();
                SetBusy(false, "对比完成");
            });
        } catch (const std::exception& ex) {
            const std::string error = ex.what();
            CallAfter([this, error] {
                report_groups_.clear();
                RefreshGrid();
                SetBusy(false, FromUtf8(error));
            });
        }
    });
}

void ComparePanel::OnExport(wxCommandEvent&) {
    if (busy_) return;
    if (report_groups_.empty()) {
        wxCommandEvent event;
        OnCompare(event);
        return;
    }
    wxFileDialog dialog(this, "导出对比 Excel", "", "compare.xlsx", "Excel workbook (*.xlsx)|*.xlsx", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK) return;
    const auto output = std::filesystem::path(dialog.GetPath().ToStdWstring());
    const auto reports = report_groups_;
    SetBusy(true, "导出中");
    worker_.Submit([this, reports, output] {
        try {
            LibXlsxWriterExporter exporter;
            exporter.ExportComparisonGroups(reports, output);
            CallAfter([this] {
                SetBusy(false, "导出完成");
            });
        } catch (const std::exception& ex) {
            const std::string error = ex.what();
            CallAfter([this, error] {
                SetBusy(false, FromUtf8(error));
            });
        }
    });
}

void ComparePanel::LoadFileInto(wxTextCtrl* target) {
    wxFileDialog dialog(this, "选择文本文件", "", "", "Text files (*.txt)|*.txt|All files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) {
        target->SetValue(dialog.GetPath());
    }
}

std::vector<ComparePanel::CompareInputGroup> ComparePanel::CurrentInputGroups() const {
    if (!input_groups_.empty()) return input_groups_;
    const auto reference = std::filesystem::path(reference_path_->GetValue().ToStdWstring());
    const auto actual = std::filesystem::path(actual_path_->GetValue().ToStdWstring());
    if (reference.empty() || actual.empty()) return {};
    CompareInputGroup group;
    group.label = ToUtf8(language_label_->GetValue());
    if (group.label.empty()) group.label = DefaultLabel(reference, 0);
    group.reference_path = reference;
    group.actual_path = actual;
    return {group};
}

CompareOptions ComparePanel::CurrentOptions() const {
    CompareOptions options;
    options.normalizer.ignore_punctuation = ignore_punctuation_->GetValue();
    options.normalizer.case_fold = true;
    options.normalizer.unicode_nfkc = true;
    options.alignment.alignment_threshold = align_threshold_->GetValue();
    options.alignment.anchor_threshold = 95.0;
    options.pass_threshold = pass_threshold_->GetValue();
    return options;
}

std::string ComparePanel::CurrentDelimiter() const {
    return ToUtf8(delimiter_->GetValue());
}

void ComparePanel::RefreshGroups() {
    group_list_->Clear();
    for (const auto& group : input_groups_) {
        group_list_->Append(FromUtf8(group.label + ": " + PathUtf8(group.reference_path) + " / " + PathUtf8(group.actual_path)));
    }
    remove_group_->Enable(!input_groups_.empty());
}

void ComparePanel::RefreshGrid() {
    result_grid_->Freeze();
    if (result_grid_->GetNumberRows() > 0) {
        result_grid_->DeleteRows(0, result_grid_->GetNumberRows());
    }
    if (result_grid_->GetNumberCols() > 0) {
        result_grid_->DeleteCols(0, result_grid_->GetNumberCols());
    }

    std::size_t max_rows = 0;
    for (const auto& group : report_groups_) {
        max_rows = (std::max)(max_rows, group.rows.size());
    }
    const int col_count = static_cast<int>(report_groups_.size() * 4);
    if (col_count > 0) {
        result_grid_->AppendCols(col_count);
    }
    if (max_rows > 0) {
        result_grid_->AppendRows(static_cast<int>(max_rows));
    }

    const char* suffixes[] = {"正式文本", "机器文本", "相似度", "结果"};
    for (std::size_t g = 0; g < report_groups_.size(); ++g) {
        const int base = static_cast<int>(g * 4);
        const auto prefix = report_groups_[g].label.empty() ? std::string{} : report_groups_[g].label + " ";
        for (int c = 0; c < 4; ++c) {
            result_grid_->SetColLabelValue(base + c, FromUtf8(prefix + suffixes[c]));
        }
        for (std::size_t r = 0; r < report_groups_[g].rows.size(); ++r) {
            const auto& row = report_groups_[g].rows[r];
            result_grid_->SetCellValue(static_cast<int>(r), base + 0, FromUtf8(row.reference_text));
            result_grid_->SetCellValue(static_cast<int>(r), base + 1, FromUtf8(row.actual_text));
            result_grid_->SetCellValue(static_cast<int>(r), base + 2, wxString::Format("%.2f", row.similarity));
            result_grid_->SetCellValue(static_cast<int>(r), base + 3, wxString::FromUTF8(StatusText(row.status)));
        }
    }
    result_grid_->AutoSizeColumns(false);
    result_grid_->Thaw();
}

void ComparePanel::SetBusy(bool busy, const wxString& message) {
    busy_ = busy;
    language_label_->Enable(!busy);
    reference_path_->Enable(!busy);
    actual_path_->Enable(!busy);
    delimiter_->Enable(!busy);
    align_threshold_->Enable(!busy);
    pass_threshold_->Enable(!busy);
    ignore_punctuation_->Enable(!busy);
    add_group_->Enable(!busy);
    remove_group_->Enable(!busy && !input_groups_.empty());
    compare_button_->Enable(!busy);
    export_button_->Enable(!busy);
    SetStatus(message);
}

void ComparePanel::SetStatus(const wxString& message) {
    status_->SetLabel(message);
}

} // namespace adayo::ui
