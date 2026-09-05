#include "ui/ComparePanel.h"

#include "adapters/excel/LibXlsxWriterExporter.h"
#include "adapters/text/TextFileImporter.h"
#include "app/ApplicationRuntime.h"
#include "platform/UnicodePath.h"
#include "platform/FileIo.h"
#include "core/unicode/Utf8.h"
#include "ui/CompareGridTable.h"
#include "ui/UiString.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/wrapsizer.h>
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
    add_text_row("字面分隔:", delimiter_, nullptr, &ComparePanel::OnOpenReference);
    delimiter_->SetToolTip(WxUtf8("UTF-8 字面字符串，不是正则表达式，不展开反斜线"));
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

    auto* options_bar=new wxWrapSizer(wxHORIZONTAL);
    auto add_choice=[&](const char* title,wxChoice*& control,std::initializer_list<const char*> labels) {
        options_bar->Add(new wxStaticText(this,wxID_ANY,WxUtf8(title)),0,wxALIGN_CENTER_VERTICAL|wxALL,4);
        control=new wxChoice(this,wxID_ANY);
        for(auto label:labels) control->Append(WxUtf8(label));
        options_bar->Add(control,0,wxALL,4);
        option_controls_.push_back(control);
        control->Bind(wxEVT_CHOICE,&ComparePanel::OnInputChanged,this);
    };
    add_choice("预设",profile_,{"现有自动对齐","逐行严格核对","字错误率 CER","词错误率 WER"});
    add_choice("对应方式",pairing_,{"顺序自动对齐","逐行对应"});
    add_choice("Unicode",normalization_,{"None","NFC","NFKC"});
    auto add_check=[&](const char* title,wxCheckBox*& control) {
        control=new wxCheckBox(this,wxID_ANY,WxUtf8(title));
        options_bar->Add(control,0,wxALIGN_CENTER_VERTICAL|wxALL,4);
        option_controls_.push_back(control);
        control->Bind(wxEVT_CHECKBOX,&ComparePanel::OnInputChanged,this);
    };
    add_check("忽略大小写",case_fold_);
    add_check("忽略标点",ignore_punctuation_);
    add_check("合并空白",collapse_whitespace_);
    add_check("去首尾空白",trim_);
    root->Add(options_bar,0,wxEXPAND|wxLEFT|wxRIGHT,10);
    auto* thresholds=new wxWrapSizer(wxHORIZONTAL);
    auto add_number=[&](const char* title,wxSpinCtrlDouble*& control,double maximum) {
        thresholds->Add(new wxStaticText(this,wxID_ANY,WxUtf8(title)),0,wxALIGN_CENTER_VERTICAL|wxALL,4);
        control=new wxSpinCtrlDouble(this,wxID_ANY,wxEmptyString,wxDefaultPosition,wxSize(90,-1));
        control->SetRange(0,maximum);
        control->SetDigits(2);
        control->Bind(wxEVT_SPINCTRLDOUBLE,&ComparePanel::OnInputChanged,this);
        control->Bind(wxEVT_TEXT,&ComparePanel::OnInputChanged,this);
        option_controls_.push_back(control);
        thresholds->Add(control,0,wxALL,4);
    };
    add_number("候选阈值",align_threshold_,100);
    add_number("锚点阈值",anchor_threshold_,100);
    add_number("唯一性差值",anchor_margin_,100);
    add_number("缺口惩罚",gap_penalty_,1000000);
    add_number("相似度通过(%)",pass_threshold_,100);
    thresholds->Add(new wxStaticText(this,wxID_ANY,WxUtf8("最大错误率(%)")),0,wxALIGN_CENTER_VERTICAL|wxALL,4);
    max_error_=new wxTextCtrl(this,wxID_ANY,"0",wxDefaultPosition,wxSize(90,-1));
    max_error_->Bind(wxEVT_TEXT,&ComparePanel::OnInputChanged,this);
    thresholds->Add(max_error_,0,wxALL,4);
    option_controls_.push_back(max_error_);
    root->Add(thresholds,0,wxEXPAND|wxLEFT|wxRIGHT,10);
    option_summary_=new wxStaticText(this,wxID_ANY,wxEmptyString);
    root->Add(option_summary_,0,wxEXPAND|wxALL,10);
    auto* commands=new wxBoxSizer(wxHORIZONTAL);
    compare_button_=new wxButton(this,wxID_ANY,WxUtf8("开始对比"));
    cancel_button_=new wxButton(this,wxID_ANY,WxUtf8("取消对比"));
    cancel_button_->Disable();
    cancel_button_->Bind(wxEVT_BUTTON,[this](wxCommandEvent&) {
        comparison_cancel_.request_stop(); cancel_button_->Disable(); SetStatus(WxUtf8("正在取消"));
    });
    export_button_=new wxButton(this,wxID_ANY,WxUtf8("导出 Excel"));
    status_=new wxStaticText(this,wxID_ANY,WxUtf8("就绪"),wxDefaultPosition,wxDefaultSize,wxST_ELLIPSIZE_END | wxST_NO_AUTORESIZE);
    status_->SetMinSize(wxSize(0,-1));
    compare_button_->Bind(wxEVT_BUTTON,&ComparePanel::OnCompare,this);
    export_button_->Bind(wxEVT_BUTTON,&ComparePanel::OnExport,this);
    for(auto* button:{compare_button_,cancel_button_,export_button_}) commands->Add(button,0,wxRIGHT,8);
    commands->Add(status_,1,wxALIGN_CENTER_VERTICAL);
    root->Add(commands,0,wxEXPAND|wxALL,10);
    ApplyOptions(runtime_.ConfigSnapshot().compare);

    result_grid_ = new wxGrid(this, wxID_ANY);
    result_grid_->SetDefaultCellOverflow(false);
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
    if(applying_options_ || !option_summary_) return;
    try {
        if(event.GetEventObject()==profile_) {
            static constexpr const char* ids[]={"legacy_v1","strict_rows_v1","asr_cer_v1","asr_wer_v1"};
            ApplyOptions(CompareService::Preset(ids[profile_->GetSelection()]));
        }
        const auto options=CurrentOptions();
        CompareService::ValidateOptions(options);
        RefreshOptionState();
        runtime_.UpdateConfig([&](AppConfig& config) { config.compare=options; });
        runtime_.SaveConfig();
    } catch(const std::exception& ex) {
        if(status_) SetStatus(WxUtf8(ex.what()));
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
    CompareOptions options;
    try { options=CurrentOptions(); CompareService::ValidateOptions(options); }
    catch(const std::exception& ex) { SetStatus(WxUtf8(ex.what())); return; }
    const auto delimiter = CurrentDelimiter();
    if (groups.empty()) {
        SetStatus(WxUtf8("没有可对比的语言组"));
        return;
    }
    const auto captured_revision = input_revision_;
    comparison_cancel_=std::stop_source{};
    auto request_cancel=comparison_cancel_;
    InvalidateReport();
    comparing_=true;
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
            if(options.profile_id!="legacy_v1") import_options.empty_records=EmptyRecordPolicy::PreserveInternal;
            std::vector<CompareExecutionContext::Reservation> retained;
            bool wer_without_boundaries=false;
            for (const auto& group : groups) {
                context.Check();
                if (token.stop_requested()) return;
                const auto reference_hash=FileSha256(group.reference_path);
                const auto actual_hash=FileSha256(group.actual_path);
                const auto reference = TextFileImporter::ReadUtf8Records(group.reference_path, import_options);
                if (token.stop_requested()) return;
                const auto actual = TextFileImporter::ReadUtf8Records(group.actual_path, import_options);
                if (token.stop_requested()) return;
                if(reference_hash!=FileSha256(group.reference_path) || actual_hash!=FileSha256(group.actual_path))
                    throw std::runtime_error("Input file changed during comparison");
                CompareReportGroup report;
                report.label=group.label;
                report.options=options;
                report.reference_source={PathToUtf8(group.reference_path),reference_hash};
                report.actual_source={PathToUtf8(group.actual_path),actual_hash};
                report.rows=service.Compare(reference,actual,options,&context);
                report.totals=CompareService::Totals(report.rows);
                std::size_t retained_bytes=CompareExecutionContext::Multiply(report.rows.size(),1024);
                for(const auto& row:report.rows) {
                    retained_bytes=CompareExecutionContext::Add(retained_bytes,
                        CompareExecutionContext::Multiply(CompareExecutionContext::Add(row.reference_text.size(),row.actual_text.size()),96));
                    if(options.metric==CompareMetric::Wer) {
                        const auto points=unicode::DecodeStrict(row.reference_text);
                        if(points.size()>1 && std::none_of(points.begin(),points.end(),unicode::IsWhitespace))
                            wer_without_boundaries=true;
                    }
                }
                retained.push_back(context.Reserve(retained_bytes,"retained comparison groups"));
                reports.push_back(std::move(report));
            }
            context.Check();
            if (token.stop_requested()) return;
            CallAfter([this, captured_revision, request_cancel, wer_without_boundaries, reports = std::move(reports)]() mutable {
                if (closing_ || IsBeingDeleted()) return;
                if(request_cancel.stop_requested()) { InvalidateReport(); SetBusy(false,WxUtf8("已取消")); return; }
                if (captured_revision != input_revision_) return;
                report_groups_ = std::make_shared<const std::vector<CompareReportGroup>>(std::move(reports));
                report_revision_ = captured_revision;
                try {
                    runtime_.UpdateConfig([&](AppConfig& config) {
                        config.compare=CurrentOptions();
                    });
                    runtime_.SaveConfig();
                } catch (const std::exception& ex) {
                    runtime_.Logger().Error("config", ex.what());
                }
                RefreshGrid();
                SetBusy(false, WxUtf8(wer_without_boundaries?"对比完成；部分文本没有空白词界，建议使用 CER":"对比完成"));
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
    comparing_=false;
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
    static constexpr const char* ids[]={"legacy_v1","strict_rows_v1","asr_cer_v1","asr_wer_v1"};
    auto options=CompareService::Preset(ids[std::max(0,profile_->GetSelection())]);
    options.pairing=pairing_->GetSelection()==0?CompareAlignment::Sequence:CompareAlignment::Rows;
    options.normalizer.normalization=static_cast<UnicodeNormalization>(normalization_->GetSelection());
    options.normalizer.case_fold=case_fold_->GetValue();
    options.normalizer.ignore_punctuation=ignore_punctuation_->GetValue();
    options.normalizer.collapse_whitespace=collapse_whitespace_->GetValue();
    options.normalizer.trim=trim_->GetValue();
    options.alignment={align_threshold_->GetValue(),anchor_threshold_->GetValue(),anchor_margin_->GetValue(),gap_penalty_->GetValue()};
    options.pass_threshold=pass_threshold_->GetValue();
    double percent=0;
    if(!max_error_->GetValue().ToDouble(&percent)) throw std::invalid_argument("最大错误率必须是有限非负数");
    options.max_error_rate=percent/100.0;
    options.delimiter=CurrentDelimiter();
    options.custom=options!=CompareService::Preset(options.profile_id);
    return options;
}

void ComparePanel::ApplyOptions(const CompareOptions& options) {
    applying_options_=true;
    profile_->SetSelection(options.profile_id=="legacy_v1"?0:options.profile_id=="strict_rows_v1"?1:options.profile_id=="asr_cer_v1"?2:3);
    pairing_->SetSelection(options.pairing==CompareAlignment::Sequence?0:1);
    normalization_->SetSelection(static_cast<int>(options.normalizer.normalization));
    case_fold_->SetValue(options.normalizer.case_fold);
    ignore_punctuation_->SetValue(options.normalizer.ignore_punctuation);
    collapse_whitespace_->SetValue(options.normalizer.collapse_whitespace);
    trim_->SetValue(options.normalizer.trim);
    align_threshold_->SetValue(options.alignment.alignment_threshold);
    anchor_threshold_->SetValue(options.alignment.anchor_threshold);
    anchor_margin_->SetValue(options.alignment.anchor_uniqueness_margin);
    gap_penalty_->SetValue(options.alignment.gap_penalty);
    pass_threshold_->SetValue(options.pass_threshold);
    max_error_->ChangeValue(wxString::Format("%.12g",options.max_error_rate*100));
    delimiter_->ChangeValue(WxUtf8(options.delimiter));
    applying_options_=false;
    RefreshOptionState();
}

void ComparePanel::RefreshOptionState() {
    const auto options=CurrentOptions();
    for(auto* control:option_controls_) control->Enable(!busy_);
    const bool sequence=options.pairing==CompareAlignment::Sequence;
    for(auto* control:{align_threshold_,anchor_threshold_,anchor_margin_,gap_penalty_}) control->Enable(!busy_ && sequence);
    pass_threshold_->Enable(!busy_ && options.metric==CompareMetric::Indel);
    max_error_->Enable(!busy_ && (options.metric==CompareMetric::Cer || options.metric==CompareMetric::Wer));
    const auto name=Utf8FromWx(profile_->GetStringSelection());
    std::string summary=options.custom?"自定义（基于"+name+"）":name;
    summary+=" | "+CompareService::ValueLabel(options.metric);
    if(sequence && (options.metric==CompareMetric::Cer || options.metric==CompareMetric::Wer)) summary+=" | 自动句对齐后 CER/WER";
    if(options.metric==CompareMetric::Indel) summary+=" | 红字为原文差异；归一化通过仍可能有红字";
    option_summary_->SetLabel(WxUtf8(summary));
    option_summary_->Wrap(std::max(300,GetClientSize().GetWidth()-24));
    Layout();
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
    RefreshOptionState();
    add_group_->Enable(!busy);
    remove_group_->Enable(!busy && !input_groups_.empty());
    compare_button_->Enable(!busy);
    cancel_button_->Enable(busy && comparing_ && !comparison_cancel_.stop_requested());
    export_button_->Enable(!busy && report_groups_ && !report_groups_->empty() && report_revision_ == input_revision_);
    SetStatus(message);
}

void ComparePanel::SetStatus(const wxString& message) {
    status_->SetLabel(message);
    status_->SetToolTip(message);
}

} // namespace adayo::ui
