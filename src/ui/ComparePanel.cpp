#include "ui/ComparePanel.h"

#include "adapters/excel/LibXlsxWriterExporter.h"
#include "adapters/text/TextFileImporter.h"
#include "app/ApplicationRuntime.h"
#include "platform/UnicodePath.h"
#include "ui/CompareGridTable.h"
#include "ui/UiString.h"

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
std::string DefaultLabel(const std::filesystem::path& path, std::size_t index) {
    const auto stem = PathToUtf8(path.stem());
    if (!stem.empty()) return stem;
    return "Group " + std::to_string(index + 1);
}
} // namespace

ComparePanel::ComparePanel(wxWindow* parent, ApplicationRuntime& runtime)
    : wxPanel(parent),
      runtime_(runtime),
      report_groups_(std::make_shared<const std::vector<CompareReportGroup>>()) {
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* input = new wxBoxSizer(wxVERTICAL);
    auto add_text_row = [&](const char* label, wxTextCtrl*& target, wxButton** browse_button, auto handler) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(this, wxID_ANY, WxUtf8(label)), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        target = new wxTextCtrl(this, wxID_ANY);
        target->Bind(wxEVT_TEXT, &ComparePanel::OnInputChanged, this);
        row->Add(target, 1, wxRIGHT, 8);
        if (browse_button) {
            auto* button = new wxButton(this, wxID_ANY, WxUtf8("打开"));
            button->Bind(wxEVT_BUTTON, handler, this);
            *browse_button = button;
            row->Add(button, 0);
        }
        input->Add(row, 0, wxEXPAND | wxBOTTOM, 6);
    };
    add_text_row("语言组:", language_label_, nullptr, &ComparePanel::OnOpenReference);
    add_text_row("正式文本:", reference_path_, &reference_browse_, &ComparePanel::OnOpenReference);
    add_text_row("机器文本:", actual_path_, &actual_browse_, &ComparePanel::OnOpenActual);
    add_text_row("固定分隔:", delimiter_, nullptr, &ComparePanel::OnOpenReference);
    root->Add(input, 0, wxEXPAND | wxALL, 10);

    auto* group_bar = new wxBoxSizer(wxHORIZONTAL);
    group_list_ = new wxListBox(this, wxID_ANY);
    add_group_ = new wxButton(this, wxID_ANY, WxUtf8("加入组"));
    remove_group_ = new wxButton(this, wxID_ANY, WxUtf8("移除组"));
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
    align_threshold_->SetValue(runtime_.ConfigSnapshot().alignment_threshold);
    align_threshold_->SetIncrement(1);
    pass_threshold_ = new wxSpinCtrlDouble(this, wxID_ANY);
    pass_threshold_->SetRange(0, 100);
    pass_threshold_->SetValue(runtime_.ConfigSnapshot().pass_threshold);
    pass_threshold_->SetIncrement(1);
    ignore_punctuation_ = new wxCheckBox(this, wxID_ANY, WxUtf8("忽略标点"));
    ignore_punctuation_->SetValue(true);
    align_threshold_->Bind(wxEVT_SPINCTRLDOUBLE, &ComparePanel::OnInputChanged, this);
    pass_threshold_->Bind(wxEVT_SPINCTRLDOUBLE, &ComparePanel::OnInputChanged, this);
    ignore_punctuation_->Bind(wxEVT_CHECKBOX, &ComparePanel::OnInputChanged, this);
    compare_button_ = new wxButton(this, wxID_ANY, WxUtf8("自动对齐"));
    cancel_button_=new wxButton(this,wxID_ANY,WxUtf8("取消对比"));
    cancel_button_->Disable();
    cancel_button_->Bind(wxEVT_BUTTON,[this](wxCommandEvent&) { comparison_cancel_.request_stop(); cancel_button_->Disable(); SetStatus(WxUtf8("正在取消")); });
    export_button_ = new wxButton(this, wxID_ANY, WxUtf8("导出 Excel"));
    status_ = new wxStaticText(this, wxID_ANY, WxUtf8("就绪"));
    compare_button_->Bind(wxEVT_BUTTON, &ComparePanel::OnCompare, this);
    export_button_->Bind(wxEVT_BUTTON, &ComparePanel::OnExport, this);
    thresholds->Add(new wxStaticText(this, wxID_ANY, WxUtf8("对齐阈值")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    thresholds->Add(align_threshold_, 0, wxRIGHT, 18);
    thresholds->Add(new wxStaticText(this, wxID_ANY, WxUtf8("OK阈值")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    thresholds->Add(pass_threshold_, 0, wxRIGHT, 18);
    thresholds->Add(ignore_punctuation_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 18);
    thresholds->Add(compare_button_, 0, wxRIGHT, 8);
    thresholds->Add(cancel_button_,0,wxRIGHT,8);
    thresholds->Add(export_button_, 0, wxRIGHT, 12);
    thresholds->Add(status_, 1, wxALIGN_CENTER_VERTICAL);
    root->Add(thresholds, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

    result_grid_ = new wxGrid(this, wxID_ANY);
    result_table_ = new CompareGridTable(report_groups_);
    result_grid_->SetTable(result_table_, true, wxGrid::wxGridSelectCells);
    result_grid_->EnableEditing(false);
    root->Add(result_grid_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    SetSizer(root);
    RefreshGroups();
    RefreshGrid();
    InvalidateReport();
}

void ComparePanel::BeginShutdown() {
    if (closing_) return;
    closing_ = true;
    comparison_cancel_.request_stop();
    busy_ = true;
    Disable();
}

void ComparePanel::OnOpenReference(wxCommandEvent&) {
    LoadFileInto(reference_path_);
}

void ComparePanel::OnOpenActual(wxCommandEvent&) {
    LoadFileInto(actual_path_);
}

void ComparePanel::OnInputChanged(wxCommandEvent& event) {
    if (align_threshold_ && pass_threshold_) {
        try {
            runtime_.UpdateConfig([&](AppConfig& config) {
                config.alignment_threshold = align_threshold_->GetValue();
                config.pass_threshold = pass_threshold_->GetValue();
            });
            runtime_.SaveConfig();
        } catch (const std::exception& ex) {
            runtime_.Logger().Error("config", ex.what());
        }
    }
    MarkInputChanged();
    event.Skip();
}

void ComparePanel::MarkInputChanged() {
    ++input_revision_;
    InvalidateReport();
}

void ComparePanel::InvalidateReport() {
    report_groups_ = std::make_shared<const std::vector<CompareReportGroup>>();
    report_revision_ = static_cast<std::uint64_t>(-1);
    RefreshGrid();
    if (export_button_) export_button_->Enable(false);
}

void ComparePanel::OnAddGroup(wxCommandEvent&) {
    const auto reference = PathFromWx(reference_path_->GetValue());
    const auto actual = PathFromWx(actual_path_->GetValue());
    if (reference.empty() || actual.empty()) {
        SetStatus(WxUtf8("请选择正式文本和机器文本"));
        return;
    }
    CompareInputGroup group;
    group.label = Utf8FromWx(language_label_->GetValue());
    if (group.label.empty()) group.label = DefaultLabel(reference, input_groups_.size());
    const auto label = group.label;
    const auto duplicate = std::any_of(input_groups_.begin(), input_groups_.end(), [&](const CompareInputGroup& item) {
        return item.label == label;
    });
    if (duplicate) {
        SetStatus(WxUtf8("语言组名称不能重复"));
        return;
    }
    group.reference_path = reference;
    group.actual_path = actual;
    input_groups_.push_back(std::move(group));
    MarkInputChanged();
    RefreshGroups();
}

void ComparePanel::OnRemoveGroup(wxCommandEvent&) {
    const int selection = group_list_->GetSelection();
    if (selection != wxNOT_FOUND && static_cast<std::size_t>(selection) < input_groups_.size()) {
        input_groups_.erase(input_groups_.begin() + selection);
        MarkInputChanged();
        RefreshGroups();
    }
}

void ComparePanel::OnCompare(wxCommandEvent&) {
    if (busy_) return;
    const auto groups = CurrentInputGroups();
    const auto options = CurrentOptions();
    const auto delimiter = CurrentDelimiter();
    if (groups.empty()) {
        SetStatus(WxUtf8("没有可对比的语言组"));
        return;
    }
    const auto captured_revision = input_revision_;
    comparison_cancel_=std::stop_source{};
    auto request_cancel=comparison_cancel_;
    InvalidateReport();
    SetBusy(true, WxUtf8("对比中"));
    runtime_.BackgroundJobs().Submit([this, groups, options, delimiter, captured_revision, request_cancel](std::stop_token token) mutable {
        try {
            std::stop_callback shutdown_callback(token,[&] { request_cancel.request_stop(); });
            CompareExecutionContext context; context.stop=request_cancel.get_token();
            context.progress=[this,captured_revision](std::size_t done,std::size_t total) {
                CallAfter([this,captured_revision,done,total] {
                    if(closing_ || !busy_ || captured_revision!=input_revision_ || comparison_cancel_.stop_requested()) return;
                    SetStatus(WxUtf8("对比中 "+std::to_string(done)+" / "+std::to_string(total)));
                });
            };
            CompareService service;
            std::vector<CompareReportGroup> reports;
            reports.reserve(groups.size());
            TextImportOptions import_options;
            import_options.delimiter = delimiter;
            for (const auto& group : groups) {
                context.Check();
                if (token.stop_requested()) return;
                const auto reference = TextFileImporter::ReadUtf8Records(group.reference_path, import_options);
                if (token.stop_requested()) return;
                const auto actual = TextFileImporter::ReadUtf8Records(group.actual_path, import_options);
                if (token.stop_requested()) return;
                reports.push_back({group.label, service.Compare(reference, actual, options,&context)});
            }
            context.Check();
            if (token.stop_requested()) return;
            CallAfter([this, captured_revision, request_cancel, reports = std::move(reports)]() mutable {
                if (closing_ || IsBeingDeleted()) return;
                if(request_cancel.stop_requested()) { InvalidateReport(); SetBusy(false,WxUtf8("已取消")); return; }
                if (captured_revision != input_revision_) return;
                report_groups_ = std::make_shared<const std::vector<CompareReportGroup>>(std::move(reports));
                report_revision_ = captured_revision;
                try {
                    runtime_.UpdateConfig([&](AppConfig& config) {
                        config.alignment_threshold = align_threshold_->GetValue();
                        config.pass_threshold = pass_threshold_->GetValue();
                    });
                    runtime_.SaveConfig();
                } catch (const std::exception& ex) {
                    runtime_.Logger().Error("config", ex.what());
                }
                RefreshGrid();
                SetBusy(false, WxUtf8("对比完成"));
            });
        } catch (const std::exception& ex) {
            const std::string error = ex.what();
            runtime_.Logger().Error("compare", error);
            CallAfter([this, error] {
                if (closing_ || IsBeingDeleted()) return;
                report_groups_ = std::make_shared<const std::vector<CompareReportGroup>>();
                RefreshGrid();
                SetBusy(false, WxUtf8(error));
            });
        }
    });
}

void ComparePanel::OnExport(wxCommandEvent&) {
    if (busy_) return;
    if (!report_groups_ || report_groups_->empty() || report_revision_ != input_revision_) {
        SetStatus(WxUtf8("请先完成当前输入的自动对齐"));
        if (export_button_) export_button_->Enable(false);
        return;
    }
    wxFileDialog dialog(this, WxUtf8("导出对比 Excel"), wxString{}, WxUtf8("compare.xlsx"), WxUtf8("Excel workbook (*.xlsx)|*.xlsx"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK) return;
    const auto output = PathFromWx(dialog.GetPath());
    const auto reports = report_groups_;
    SetBusy(true, WxUtf8("导出中"));
    runtime_.BackgroundJobs().Submit([this, reports, output](std::stop_token token) {
        try {
            if (token.stop_requested()) return;
            LibXlsxWriterExporter exporter;
            exporter.ExportComparisonGroups(*reports, output);
            if (token.stop_requested()) return;
            CallAfter([this] {
                if (closing_ || IsBeingDeleted()) return;
                SetBusy(false, WxUtf8("导出完成"));
            });
        } catch (const std::exception& ex) {
            const std::string error = ex.what();
            runtime_.Logger().Error("export", error);
            CallAfter([this, error] {
                if (closing_ || IsBeingDeleted()) return;
                SetBusy(false, WxUtf8(error));
            });
        }
    });
}

void ComparePanel::LoadFileInto(wxTextCtrl* target) {
    wxFileDialog dialog(this, WxUtf8("选择文本文件"), wxString{}, wxString{}, WxUtf8("Text files (*.txt)|*.txt|All files (*.*)|*.*"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) {
        target->SetValue(dialog.GetPath());
        MarkInputChanged();
    }
}

std::vector<ComparePanel::CompareInputGroup> ComparePanel::CurrentInputGroups() const {
    if (!input_groups_.empty()) return input_groups_;
    const auto reference = PathFromWx(reference_path_->GetValue());
    const auto actual = PathFromWx(actual_path_->GetValue());
    if (reference.empty() || actual.empty()) return {};
    CompareInputGroup group;
    group.label = Utf8FromWx(language_label_->GetValue());
    if (group.label.empty()) group.label = DefaultLabel(reference, 0);
    group.reference_path = reference;
    group.actual_path = actual;
    return {group};
}

CompareOptions ComparePanel::CurrentOptions() const {
    CompareOptions options;
    options.normalizer.ignore_punctuation = ignore_punctuation_->GetValue();
    options.normalizer.case_fold = true;
    options.normalizer.normalization = UnicodeNormalization::Nfkc;
    options.alignment.alignment_threshold = align_threshold_->GetValue();
    options.alignment.anchor_threshold = 95.0;
    options.pass_threshold = pass_threshold_->GetValue();
    return options;
}

std::string ComparePanel::CurrentDelimiter() const {
    return Utf8FromWx(delimiter_->GetValue());
}

void ComparePanel::RefreshGroups() {
    group_list_->Clear();
    for (const auto& group : input_groups_) {
        group_list_->Append(WxUtf8(group.label + ": " + PathToUtf8(group.reference_path) + " / " + PathToUtf8(group.actual_path)));
    }
    remove_group_->Enable(!input_groups_.empty());
}

void ComparePanel::RefreshGrid() {
    result_grid_->Freeze();
    const int old_rows = result_grid_->GetNumberRows();
    const int old_cols = result_grid_->GetNumberCols();
    result_table_->SetReports(report_groups_);
    const int new_rows = result_table_->GetNumberRows();
    const int new_cols = result_table_->GetNumberCols();
    if (old_rows > new_rows) {
        wxGridTableMessage msg(result_table_, wxGRIDTABLE_NOTIFY_ROWS_DELETED, new_rows, old_rows - new_rows);
        result_grid_->ProcessTableMessage(msg);
    } else if (old_rows < new_rows) {
        wxGridTableMessage msg(result_table_, wxGRIDTABLE_NOTIFY_ROWS_APPENDED, new_rows - old_rows);
        result_grid_->ProcessTableMessage(msg);
    }
    if (old_cols > new_cols) {
        wxGridTableMessage msg(result_table_, wxGRIDTABLE_NOTIFY_COLS_DELETED, new_cols, old_cols - new_cols);
        result_grid_->ProcessTableMessage(msg);
    } else if (old_cols < new_cols) {
        wxGridTableMessage msg(result_table_, wxGRIDTABLE_NOTIFY_COLS_APPENDED, new_cols - old_cols);
        result_grid_->ProcessTableMessage(msg);
    }
    for (int c = 0; c < result_grid_->GetNumberCols(); ++c) {
        result_grid_->SetColSize(c, (c % 4 == 2 || c % 4 == 3) ? 90 : 260);
    }
    result_grid_->ForceRefresh();
    result_grid_->Thaw();
}

void ComparePanel::SetBusy(bool busy, const wxString& message) {
    busy_ = busy;
    language_label_->Enable(!busy);
    reference_path_->Enable(!busy);
    actual_path_->Enable(!busy);
    if (reference_browse_) reference_browse_->Enable(!busy);
    if (actual_browse_) actual_browse_->Enable(!busy);
    delimiter_->Enable(!busy);
    align_threshold_->Enable(!busy);
    pass_threshold_->Enable(!busy);
    ignore_punctuation_->Enable(!busy);
    add_group_->Enable(!busy);
    remove_group_->Enable(!busy && !input_groups_.empty());
    compare_button_->Enable(!busy);
    cancel_button_->Enable(busy && !comparison_cancel_.stop_requested());
    export_button_->Enable(!busy && report_groups_ && !report_groups_->empty() && report_revision_ == input_revision_);
    SetStatus(message);
}

void ComparePanel::SetStatus(const wxString& message) {
    status_->SetLabel(message);
}

} // namespace adayo::ui
