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
#include <cmath>

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
#include <wx/collpane.h>
#include <wx/richtext/richtextctrl.h>
#include <wx/msgdlg.h>

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

    auto* group_bar = new wxBoxSizer(wxHORIZONTAL);
    group_list_ = new wxListBox(this,wxID_ANY,wxDefaultPosition,FromDIP(wxSize(170,110)),0,nullptr,wxLB_SINGLE|wxLB_HSCROLL);
    group_list_->Bind(wxEVT_LISTBOX,[this](wxCommandEvent&) { SelectInputGroup(); });
    group_bar->Add(group_list_,0,wxEXPAND|wxRIGHT,8);
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
    group_bar->Add(input,1,wxEXPAND);
    root->Add(group_bar,0,wxEXPAND|wxALL,8);
    add_group_ = new wxButton(this, wxID_ANY, WxUtf8("加入组"));
    remove_group_ = new wxButton(this, wxID_ANY, WxUtf8("移除组"));
    new_group_=new wxButton(this,wxID_ANY,WxUtf8("新建组"));
    apply_group_=new wxButton(this,wxID_ANY,WxUtf8("应用修改"));
    cancel_group_=new wxButton(this,wxID_ANY,WxUtf8("取消修改"));
    new_group_->Bind(wxEVT_BUTTON,[this](wxCommandEvent&) { if(!busy_ && ResolveGroupDraft()) LoadGroupDraft({}); });
    apply_group_->Bind(wxEVT_BUTTON,[this](wxCommandEvent&) { if(!busy_) ApplyGroupDraft(); });
    cancel_group_->Bind(wxEVT_BUTTON,[this](wxCommandEvent&) { if(!busy_) LoadGroupDraft(editing_group_id_); });
    add_group_->Bind(wxEVT_BUTTON, &ComparePanel::OnAddGroup, this);
    remove_group_->Bind(wxEVT_BUTTON, &ComparePanel::OnRemoveGroup, this);
    auto* group_buttons = new wxBoxSizer(wxHORIZONTAL);
    for(auto* button:{new_group_,add_group_,apply_group_,cancel_group_,remove_group_}) group_buttons->Add(button,0,wxRIGHT,6);
    root->Add(group_buttons,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,8);

    auto* options_bar=new wxWrapSizer(wxHORIZONTAL);
    auto add_choice=[&](wxWindow* parent,wxSizer* sizer,const char* title,wxChoice*& control,std::initializer_list<const char*> labels) {
        sizer->Add(new wxStaticText(parent,wxID_ANY,WxUtf8(title)),0,wxALIGN_CENTER_VERTICAL|wxALL,4);
        control=new wxChoice(parent,wxID_ANY);
        for(auto label:labels) control->Append(WxUtf8(label));
        sizer->Add(control,0,wxALL,4);
        option_controls_.push_back(control);
        control->Bind(wxEVT_CHOICE,&ComparePanel::OnInputChanged,this);
    };
    add_choice(this,options_bar,"预设",profile_,{"现有自动对齐","逐行严格核对","字错误率 CER","词错误率 WER"});
    add_choice(this,options_bar,"对应方式",pairing_,{"顺序自动对齐","逐行对应"});
    auto add_number=[&](wxWindow* parent,wxSizer* sizer,const char* title,wxSpinCtrlDouble*& control,double maximum) {
        sizer->Add(new wxStaticText(parent,wxID_ANY,WxUtf8(title)),0,wxALIGN_CENTER_VERTICAL|wxALL,4);
        control=new wxSpinCtrlDouble(parent,wxID_ANY,wxEmptyString,wxDefaultPosition,FromDIP(wxSize(90,-1)));
        control->SetRange(0,maximum); control->SetDigits(2);
        control->Bind(wxEVT_SPINCTRLDOUBLE,&ComparePanel::OnInputChanged,this);
        control->Bind(wxEVT_TEXT,&ComparePanel::OnInputChanged,this);
        control->Bind(wxEVT_KILL_FOCUS,[this](wxFocusEvent& event) { CommitOptions(); event.Skip(); });
        option_controls_.push_back(control); sizer->Add(control,0,wxALL,4);
    };
    add_number(this,options_bar,"相似度通过 (%)",pass_threshold_,100);
    options_bar->Add(new wxStaticText(this,wxID_ANY,WxUtf8("最大错误率 (%)")),0,wxALIGN_CENTER_VERTICAL|wxALL,4);
    max_error_=new wxTextCtrl(this,wxID_ANY,"0",wxDefaultPosition,FromDIP(wxSize(85,-1)));
    max_error_->Bind(wxEVT_TEXT,&ComparePanel::OnInputChanged,this);
    max_error_->Bind(wxEVT_KILL_FOCUS,[this](wxFocusEvent& event) { CommitOptions(); event.Skip(); });
    options_bar->Add(max_error_,0,wxALL,4); option_controls_.push_back(max_error_);
    root->Add(options_bar,0,wxEXPAND|wxLEFT|wxRIGHT,8);
    advanced_=new wxCollapsiblePane(this,wxID_ANY,WxUtf8("高级参数"),wxDefaultPosition,wxDefaultSize,wxCP_DEFAULT_STYLE|wxCP_NO_TLW_RESIZE);
    auto* advanced_parent=advanced_->GetPane();
    auto* advanced_sizer=new wxBoxSizer(wxVERTICAL);
    auto* normalizers=new wxWrapSizer(wxHORIZONTAL);
    add_choice(advanced_parent,normalizers,"Unicode",normalization_,{"None","NFC","NFKC"});
    auto add_check=[&](const char* title,wxCheckBox*& control) {
        control=new wxCheckBox(advanced_parent,wxID_ANY,WxUtf8(title));
        normalizers->Add(control,0,wxALIGN_CENTER_VERTICAL|wxALL,4);
        option_controls_.push_back(control);
        control->Bind(wxEVT_CHECKBOX,&ComparePanel::OnInputChanged,this);
    };
    add_check("忽略大小写",case_fold_);
    add_check("忽略标点",ignore_punctuation_);
    add_check("合并空白",collapse_whitespace_);
    add_check("去首尾空白",trim_);
    advanced_sizer->Add(normalizers,0,wxEXPAND);
    auto* thresholds=new wxWrapSizer(wxHORIZONTAL);
    add_number(advanced_parent,thresholds,"候选阈值",align_threshold_,100);
    add_number(advanced_parent,thresholds,"锚点阈值",anchor_threshold_,100);
    add_number(advanced_parent,thresholds,"唯一性差值",anchor_margin_,100);
    add_number(advanced_parent,thresholds,"缺口惩罚",gap_penalty_,1000000);
    advanced_sizer->Add(thresholds,0,wxEXPAND);
    auto* delimiter_row=new wxBoxSizer(wxHORIZONTAL);
    delimiter_row->Add(new wxStaticText(advanced_parent,wxID_ANY,WxUtf8("字面分隔")),0,wxALIGN_CENTER_VERTICAL|wxRIGHT,6);
    delimiter_=new wxTextCtrl(advanced_parent,wxID_ANY);
    delimiter_->Bind(wxEVT_TEXT,&ComparePanel::OnInputChanged,this);
    delimiter_->Bind(wxEVT_KILL_FOCUS,[this](wxFocusEvent& event) { CommitOptions(); event.Skip(); });
    delimiter_row->Add(delimiter_,1); advanced_sizer->Add(delimiter_row,0,wxEXPAND|wxALL,4);
    advanced_parent->SetSizer(advanced_sizer);
    advanced_->Bind(wxEVT_COLLAPSIBLEPANE_CHANGED,[this](wxCollapsiblePaneEvent&) { LayoutOptions(); });
    root->Add(advanced_,0,wxEXPAND|wxLEFT|wxRIGHT,8);
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
    observed_options_=CurrentOptions();

    auto* result_bar=new wxBoxSizer(wxHORIZONTAL);
    result_group_=new wxChoice(this,wxID_ANY);
    result_group_->Bind(wxEVT_CHOICE,[this](wxCommandEvent&) {
        if(result_group_->GetSelection()!=wxNOT_FOUND) { result_table_->SetCurrentGroup(result_group_->GetSelection()); RefreshGrid(); }
    });
    result_summary_=new wxStaticText(this,wxID_ANY,wxString{},wxDefaultPosition,wxDefaultSize,wxST_ELLIPSIZE_END|wxST_NO_AUTORESIZE);
    result_summary_->SetMinSize(wxSize(0,-1));
    result_bar->Add(new wxStaticText(this,wxID_ANY,WxUtf8("结果组")),0,wxALIGN_CENTER_VERTICAL|wxRIGHT,6);
    result_bar->Add(result_group_,0,wxRIGHT,8); result_bar->Add(result_summary_,1,wxALIGN_CENTER_VERTICAL);
    root->Add(result_bar,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,8);

    result_grid_ = new wxGrid(this, wxID_ANY);
    result_grid_->SetDefaultCellOverflow(false);
    result_table_ = new CompareGridTable(report_groups_);
    result_grid_->SetTable(result_table_, true, wxGrid::wxGridSelectCells);
    result_grid_->EnableEditing(false);
    result_grid_->Bind(wxEVT_GRID_SELECT_CELL,[this](wxGridEvent& event) { ShowDiffDetails(event.GetRow()); event.Skip(); });
    root->Add(result_grid_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    auto* details=new wxBoxSizer(wxHORIZONTAL);
    auto detail=[&](const char* label,wxStaticText*& title,wxRichTextCtrl*& text) {
        auto* box=new wxBoxSizer(wxVERTICAL);
        title=new wxStaticText(this,wxID_ANY,WxUtf8(label));
        text=new wxRichTextCtrl(this,wxID_ANY,wxString{},wxDefaultPosition,FromDIP(wxSize(-1,145)),wxRE_MULTILINE|wxRE_READONLY);
        box->Add(title,0,wxBOTTOM,4); box->Add(text,1,wxEXPAND);
        details->Add(box,1,wxEXPAND|wxRIGHT,6);
    };
    detail("正式原文",reference_detail_label_,reference_detail_);
    detail("机器原文",actual_detail_label_,actual_detail_);
    root->Add(details,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,8);
    SetSizer(root);
    Bind(wxEVT_SIZE,[this](wxSizeEvent& event) { LayoutOptions(); event.Skip(); });
    RefreshGroups();
    RefreshGrid();
    InvalidateReport();
}

void ComparePanel::LayoutOptions() {
    if (!GetSizer()) return;
    Layout();
    if (advanced_->IsExpanded()) {
        // Wrapping needs the actual pane width before its cached best height is valid.
        advanced_->GetPane()->InvalidateBestSize();
        advanced_->InvalidateBestSize();
        Layout();
    }
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
    if(event.GetEventObject()==language_label_ || event.GetEventObject()==reference_path_ || event.GetEventObject()==actual_path_) {
        if(!filling_draft_) {
            UpdateDraftState();
            if(input_groups_.empty()) MarkInputChanged();
        }
        event.Skip(); return;
    }
    if(event.GetEventObject()==profile_) {
        static constexpr const char* ids[]={"legacy_v1","strict_rows_v1","asr_cer_v1","asr_wer_v1"};
        ApplyOptions(CompareService::Preset(ids[profile_->GetSelection()]));
    }
    ObserveOptions();
    if(event.GetEventType()!=wxEVT_TEXT) CommitOptions();
    event.Skip();
}

void ComparePanel::ObserveOptions() {
    if(applying_options_ || closing_) return;
    try {
        const auto options=CurrentOptions(); CompareService::ValidateOptions(options);
        const bool changed=!options_valid_ || !observed_options_ || options!=*observed_options_;
        observed_options_=options; options_valid_=true;
        if(changed) MarkInputChanged();
        RefreshOptionState();
    } catch(const std::exception& ex) {
        if(options_valid_) MarkInputChanged();
        options_valid_=false;
        option_summary_->SetLabel(WxUtf8("参数无效："+std::string(ex.what())));
        if(compare_button_) compare_button_->Disable();
        if(status_) SetStatus(WxUtf8(ex.what()));
    }
}

bool ComparePanel::CommitOptions() {
    if(applying_options_ || closing_ || busy_ || !option_summary_) return false;
    ObserveOptions();
    if(!options_valid_) return false;
    if(!runtime_.ConfigSaveAllowed()) return true;
    const auto previous=runtime_.ConfigSnapshot().compare;
    const auto options=CurrentOptions();
    if(previous==options) return true;
    try {
        runtime_.SaveConfig([&](AppConfig& config) { config.compare=options; },
            [](AppConfig& config,const AppConfig& before) { config.compare=before.compare; });
        return true;
    } catch(const std::exception& ex) {
        SetStatus(WxUtf8("参数保存失败："+std::string(ex.what()))); return false;
    }
}

void ComparePanel::UpdateDraftState() {
    const auto found=std::find_if(input_groups_.begin(),input_groups_.end(),[&](const auto& group) { return editing_group_id_ && group.id==*editing_group_id_; });
    if(found==input_groups_.end()) draft_dirty_=!language_label_->IsEmpty() || !reference_path_->IsEmpty() || !actual_path_->IsEmpty();
    else draft_dirty_=found->label!=Utf8FromWx(language_label_->GetValue()) || found->reference_path!=PathFromWx(reference_path_->GetValue()) || found->actual_path!=PathFromWx(actual_path_->GetValue());
    add_group_->Enable(!busy_ && !editing_group_id_);
    apply_group_->Enable(!busy_ && editing_group_id_.has_value() && draft_dirty_);
    cancel_group_->Enable(!busy_ && draft_dirty_);
    remove_group_->Enable(!busy_ && editing_group_id_.has_value());
    if(compare_button_) compare_button_->Enable(!busy_ && options_valid_ && (input_groups_.empty() || !draft_dirty_));
}

void ComparePanel::LoadGroupDraft(std::optional<std::uint64_t> id) {
    const auto old_label=language_label_->GetValue(),old_reference=reference_path_->GetValue(),old_actual=actual_path_->GetValue();
    filling_draft_=true;
    editing_group_id_=id;
    const auto found=std::find_if(input_groups_.begin(),input_groups_.end(),[&](const auto& group) { return id && group.id==*id; });
    if(found==input_groups_.end()) {
        editing_group_id_.reset(); language_label_->ChangeValue({}); reference_path_->ChangeValue({}); actual_path_->ChangeValue({});
        group_list_->SetSelection(wxNOT_FOUND);
    } else {
        language_label_->ChangeValue(WxUtf8(found->label));
        reference_path_->ChangeValue(WxUtf8(PathToUtf8(found->reference_path)));
        actual_path_->ChangeValue(WxUtf8(PathToUtf8(found->actual_path)));
        group_list_->SetSelection(static_cast<int>(found-input_groups_.begin()));
    }
    filling_draft_=false; UpdateDraftState();
    if(input_groups_.empty() && (old_label!=language_label_->GetValue() || old_reference!=reference_path_->GetValue() || old_actual!=actual_path_->GetValue())) MarkInputChanged();
}

bool ComparePanel::ResolveGroupDraft() {
    if(!draft_dirty_) return true;
    wxMessageDialog dialog(this,WxUtf8("当前组有未应用的输入修改。"),WxUtf8("组草稿"),wxYES_NO|wxCANCEL|wxCANCEL_DEFAULT|wxICON_QUESTION);
    dialog.SetYesNoCancelLabels(WxUtf8("应用"),WxUtf8("放弃草稿"),WxUtf8("取消"));
    const auto answer=dialog.ShowModal();
    if(answer==wxID_YES) return ApplyGroupDraft();
    return answer==wxID_NO;
}

void ComparePanel::SelectInputGroup() {
    if(busy_) return;
    const int selection=group_list_->GetSelection();
    if(selection==wxNOT_FOUND) return;
    const auto requested=input_groups_.at(selection).id;
    if(editing_group_id_==requested) return;
    if(ResolveGroupDraft()) LoadGroupDraft(requested);
    else {
        int previous=wxNOT_FOUND;
        for(std::size_t i=0;i<input_groups_.size();++i) if(editing_group_id_==input_groups_[i].id) previous=static_cast<int>(i);
        group_list_->SetSelection(previous);
    }
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
    if(!busy_ && !editing_group_id_) ApplyGroupDraft();
}

bool ComparePanel::ApplyGroupDraft() {
    const auto reference = PathFromWx(reference_path_->GetValue());
    const auto actual = PathFromWx(actual_path_->GetValue());
    if (reference.empty() || actual.empty()) {
        SetStatus(WxUtf8("请选择正式文本和机器文本"));
        return false;
    }
    CompareInputGroup group;
    group.id=editing_group_id_.value_or(next_group_id_+1);
    group.label = Utf8FromWx(language_label_->GetValue());
    if (group.label.empty()) group.label = DefaultLabel(reference, input_groups_.size());
    const auto label = group.label;
    const auto duplicate = std::any_of(input_groups_.begin(), input_groups_.end(), [&](const CompareInputGroup& item) {
        return item.label == label && item.id!=group.id;
    });
    if (duplicate) {
        SetStatus(WxUtf8("语言组名称不能重复"));
        return false;
    }
    group.reference_path = reference;
    group.actual_path = actual;
    auto found=std::find_if(input_groups_.begin(),input_groups_.end(),[&](const auto& item) { return item.id==group.id; });
    bool changed=false;
    if(found==input_groups_.end()) { ++next_group_id_; input_groups_.push_back(group); changed=true; }
    else if(found->label!=group.label || found->reference_path!=group.reference_path || found->actual_path!=group.actual_path) { *found=group; changed=true; }
    if(changed) MarkInputChanged();
    RefreshGroups();
    LoadGroupDraft(group.id);
    return true;
}

void ComparePanel::OnRemoveGroup(wxCommandEvent&) {
    if(busy_ || !editing_group_id_) return;
    const auto id=*editing_group_id_;
    if(!ResolveGroupDraft()) return;
    const auto found=std::find_if(input_groups_.begin(),input_groups_.end(),[&](const auto& group) { return group.id==id; });
    if(found!=input_groups_.end()) {
        input_groups_.erase(found);
        MarkInputChanged();
        RefreshGroups();
        LoadGroupDraft({});
    }
}

void ComparePanel::OnCompare(wxCommandEvent&) {
    if (busy_) return;
    if(!input_groups_.empty() && draft_dirty_) { SetStatus(WxUtf8("有未应用的组草稿")); return; }
    if(!CommitOptions()) return;
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
    auto number=[](wxSpinCtrlDouble* control) {
        double result{};
        if(!control->GetTextValue().ToDouble(&result) || !std::isfinite(result) || result<control->GetMin() || result>control->GetMax())
            throw std::invalid_argument("数值参数无效或超出范围");
        return result;
    };
    options.alignment={number(align_threshold_),number(anchor_threshold_),number(anchor_margin_),number(gap_penalty_)};
    options.pass_threshold=number(pass_threshold_);
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
    if(options.metric==CompareMetric::Indel) summary+=" | 判定：归一化相似度";
    if(options.metric==CompareMetric::Exact) summary+=" | 判定：原文完全一致";
    option_summary_->SetLabel(WxUtf8(summary));
    option_summary_->Wrap(std::max(300,GetClientSize().GetWidth()-24));
    if(compare_button_) compare_button_->Enable(!busy_ && options_valid_ && (input_groups_.empty() || !draft_dirty_));
    Layout();
}

std::string ComparePanel::CurrentDelimiter() const {
    return Utf8FromWx(delimiter_->GetValue());
}

void ComparePanel::RefreshGroups() {
    group_list_->Clear();
    for (const auto& group : input_groups_) {
        const auto index=group_list_->Append(WxUtf8(group.label));
        if(editing_group_id_==group.id) group_list_->SetSelection(static_cast<int>(index));
    }
    UpdateDraftState();
}

void ComparePanel::RefreshGrid() {
    if(!result_grid_ || !result_table_) return;
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
    result_grid_->SetColLabelSize(2 * result_grid_->GetCharHeight() + 12);
    for (int c = 0; c < result_grid_->GetNumberCols(); ++c) {
        result_grid_->SetColSize(c, FromDIP(c==2 ? 155 : (c==3 ? 85 : 300)));
    }
    result_grid_->ForceRefresh();
    result_grid_->Thaw();
    result_group_->Clear();
    for(const auto& report:*report_groups_) result_group_->Append(WxUtf8(report.label));
    result_group_->Enable(!report_groups_->empty());
    if(!report_groups_->empty()) {
        result_group_->SetSelection(static_cast<int>(result_table_->CurrentGroup()));
        const auto& report=report_groups_->at(result_table_->CurrentGroup());
        std::size_t ok{},ng{},missing{},extra{};
        for(const auto& row:report.rows) {
            switch(row.status) {
                case CompareStatus::Ok: ++ok; break;
                case CompareStatus::Ng: ++ng; break;
                case CompareStatus::Missing: ++missing; break;
                case CompareStatus::Extra: ++extra; break;
            }
        }
        const auto& t=report.totals;
        auto summary="OK "+std::to_string(ok)+" / NG "+std::to_string(ng)+" / MISSING "+std::to_string(missing)+" / EXTRA "+std::to_string(extra);
        summary+=" | S/D/I/N "+std::to_string(t.substitutions)+"/"+std::to_string(t.deletions)+"/"+std::to_string(t.insertions)+"/"+std::to_string(t.reference_units);
        if(report.options.metric==CompareMetric::Cer || report.options.metric==CompareMetric::Wer) {
            const auto rate=t.ErrorRate();
            summary+=" | "+CompareService::ValueLabel(report.options.metric)+": "+(rate ? Utf8FromWx(wxString::Format("%.2f%%",*rate*100)) : std::string("N=0，未定义"));
        }
        result_summary_->SetLabel(WxUtf8(summary)); result_summary_->SetToolTip(WxUtf8(summary));
    } else result_summary_->SetLabel({});
    ShowDiffDetails(result_grid_->GetGridCursorRow());
}

void ComparePanel::ShowDiffDetails(int row) {
    if(!reference_detail_ || !actual_detail_) return;
    reference_detail_->Clear(); actual_detail_->Clear();
    reference_detail_label_->SetLabel(WxUtf8("正式原文")); actual_detail_label_->SetLabel(WxUtf8("机器原文"));
    if(report_groups_->empty() || row<0 || static_cast<std::size_t>(row)>=report_groups_->at(result_table_->CurrentGroup()).rows.size()) return;
    const auto& item=report_groups_->at(result_table_->CurrentGroup()).rows.at(row);
    auto render=[](wxRichTextCtrl* control,const std::string& raw,const std::vector<DiffFragment>& fragments) {
        std::string reconstructed;
        for(const auto& fragment:fragments) reconstructed+=fragment.text;
        if(reconstructed!=raw) throw std::runtime_error("原文差异片段无法完整重建");
        control->Freeze();
        for(const auto& fragment:fragments) {
            control->BeginTextColour(fragment.kind==DiffKind::Same ? wxColour(24,24,24) : wxColour(190,30,40));
            control->WriteText(WxUtf8(fragment.text));
            control->EndTextColour();
        }
        control->SetInsertionPoint(0); control->ShowPosition(0); control->Thaw();
    };
    try {
        if(item.reference_index) render(reference_detail_,item.reference_text,item.diff.reference_fragments);
        else reference_detail_label_->SetLabel(WxUtf8("正式侧不存在记录（EXTRA）"));
        if(item.actual_index) render(actual_detail_,item.actual_text,item.diff.actual_fragments);
        else actual_detail_label_->SetLabel(WxUtf8("机器侧不存在记录（MISSING）"));
    } catch(const std::exception& ex) { SetStatus(WxUtf8(ex.what())); }
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
    group_list_->Enable(!busy); new_group_->Enable(!busy);
    UpdateDraftState();
    cancel_button_->Enable(busy && comparing_ && !comparison_cancel_.stop_requested());
    export_button_->Enable(!busy && report_groups_ && !report_groups_->empty() && report_revision_ == input_revision_);
    SetStatus(message);
}

void ComparePanel::SetStatus(const wxString& message) {
    status_->SetLabel(message);
    status_->SetToolTip(message);
}

} // namespace adayo::ui
