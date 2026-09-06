#include "ui/VoiceSelectionDialog.h"
#include "app/ApplicationRuntime.h"
#include "platform/FileIo.h"
#include "platform/UnicodePath.h"
#include "ui/UiString.h"
#include <algorithm>
#include <map>
#include <wx/app.h>
#include <wx/button.h>
#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace adayo::ui {
VoiceSelectionDialog::VoiceSelectionDialog(wxWindow* parent, ApplicationRuntime& runtime,
    const std::vector<TtsModelEntry>& entries,std::string locale,std::string current_id)
    : wxDialog(parent,wxID_ANY,WxUtf8("选择声音"),wxDefaultPosition,wxDefaultSize,wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER),
      runtime_(runtime),entries_(entries),locale_(std::move(locale)),current_id_(std::move(current_id)) {
    for(std::size_t i=0;i<entries_.size();++i) {
        const auto& entry=entries_[i];
        if(entry.config.language_code!=locale_) continue;
        auto group=std::find_if(groups_.begin(),groups_.end(),[&](const auto& g) { return g.root==entry.root; });
        if(group==groups_.end()) { groups_.push_back({entry.root,entry.display_name,{i}}); }
        else { group->entries.push_back(i); if(!entry.speakers.empty()) group->name=entry.display_name; }
    }
    auto* root=new wxBoxSizer(wxVERTICAL);
    root->Add(new wxStaticText(this,wxID_ANY,WxUtf8("有效语言："+(locale_.empty()?std::string("未识别"):locale_))),0,wxALL,8);
    search_=new wxTextCtrl(this,wxID_ANY);
    search_->SetHint(WxUtf8("搜索声音、speaker 或 ID"));
    root->Add(search_,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,8);
    auto* lists=new wxBoxSizer(wxHORIZONTAL);
    models_=new wxListBox(this,wxID_ANY,wxDefaultPosition,wxDefaultSize,0,nullptr,wxLB_SINGLE|wxLB_HSCROLL);
    speakers_=new wxListBox(this,wxID_ANY,wxDefaultPosition,wxDefaultSize,0,nullptr,wxLB_SINGLE|wxLB_HSCROLL);
    models_->SetName(WxUtf8("模型资源")); speakers_->SetName(WxUtf8("Speaker"));
    lists->Add(models_,1,wxEXPAND|wxRIGHT,8); lists->Add(speakers_,1,wxEXPAND);
    root->Add(lists,1,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,8);
    details_=new wxTextCtrl(this,wxID_ANY,wxString{},wxDefaultPosition,FromDIP(wxSize(-1,145)),wxTE_MULTILINE|wxTE_READONLY|wxTE_DONTWRAP);
    root->Add(details_,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,8);
    auto* buttons=new wxStdDialogButtonSizer;
    confirm_=new wxButton(this,wxID_OK,WxUtf8("确认选择"));
    buttons->AddButton(confirm_); buttons->AddButton(new wxButton(this,wxID_CANCEL,WxUtf8("取消"))); buttons->Realize();
    root->Add(buttons,0,wxALIGN_RIGHT|wxALL,8);
    SetSizer(root); SetMinSize(FromDIP(wxSize(640,430))); SetSize(FromDIP(wxSize(760,540))); CentreOnParent();
    search_->Bind(wxEVT_TEXT,[this](wxCommandEvent&) { Filter(); });
    models_->Bind(wxEVT_LISTBOX,[this](wxCommandEvent&) { ShowSpeakers(); });
    speakers_->Bind(wxEVT_LISTBOX,[this](wxCommandEvent&) { current_id_=SelectedId(); ShowDetails(); });
    confirm_->Bind(wxEVT_BUTTON,[this](wxCommandEvent&) { if(!SelectedId().empty()) EndModal(wxID_OK); });
    Filter(); search_->SetFocus();
}

bool VoiceSelectionDialog::Matches(const TtsModelEntry& entry) const {
    auto haystack=WxUtf8(entry.display_name+" "+entry.id+" "+std::to_string(entry.config.speaker_id));
    for(const auto& [id,name]:entry.speakers)
        if(id==entry.config.speaker_id) haystack+=" "+WxUtf8(name);
    return haystack.Lower().Contains(search_->GetValue().Lower());
}

void VoiceSelectionDialog::Filter() {
    std::filesystem::path selected_root;
    if(models_->GetSelection()!=wxNOT_FOUND) selected_root=groups_[shown_groups_.at(models_->GetSelection())].root;
    else for(const auto& entry:entries_) if(entry.id==current_id_) selected_root=entry.root;
    models_->Clear(); shown_groups_.clear();
    for(std::size_t i=0;i<groups_.size();++i) {
        const auto& group=groups_[i];
        if(!std::any_of(group.entries.begin(),group.entries.end(),[&](auto entry) { return Matches(entries_[entry]); })) continue;
        shown_groups_.push_back(i);
        const auto index=models_->Append(WxUtf8(group.name+" / "+locale_+" ("+std::to_string(group.entries.size())+")"));
        if(group.root==selected_root) models_->SetSelection(static_cast<int>(index));
    }
    ShowSpeakers();
}

void VoiceSelectionDialog::ShowSpeakers() {
    speakers_->Clear(); shown_entries_.clear();
    if(models_->GetSelection()!=wxNOT_FOUND) {
        const auto& group=groups_[shown_groups_.at(models_->GetSelection())];
        std::map<int,std::string> names;
        for(auto owner:group.entries)
            for(const auto& [sid,label]:entries_[owner].speakers) names.emplace(sid,label);
        for(auto index:group.entries) {
            const auto& entry=entries_[index];
            if(!Matches(entry) && !WxUtf8(group.name).Lower().Contains(search_->GetValue().Lower())) continue;
            std::string name=entry.display_name;
            if(const auto found=names.find(entry.config.speaker_id);found!=names.end()) name=found->second;
            shown_entries_.push_back(index);
            const auto row=speakers_->Append(WxUtf8(name+" / speaker "+std::to_string(entry.config.speaker_id)));
            if(entry.id==current_id_) speakers_->SetSelection(static_cast<int>(row));
        }
    }
    ShowDetails();
}

std::string VoiceSelectionDialog::SelectedId() const {
    if(speakers_->GetSelection()==wxNOT_FOUND) return {};
    return entries_[shown_entries_.at(speakers_->GetSelection())].id;
}

void VoiceSelectionDialog::ShowDetails() {
    const auto generation=++*detail_generation_;
    const auto selected=speakers_->GetSelection();
    confirm_->Enable(selected!=wxNOT_FOUND);
    if(selected==wxNOT_FOUND) { details_->ChangeValue(WxUtf8("未选择声音")); return; }
    const auto entry=entries_[shown_entries_.at(selected)];
    std::string text=entry.display_name+" / "+entry.config.language_code+" / speaker "+std::to_string(entry.config.speaker_id)+
        "\nID: "+entry.id+"\n"+PathToUtf8(entry.root)+"\n"+entry.config.model_path;
    if(!entry.observation_error.empty()) text+="\n观察记录不可用："+entry.observation_error;
    else if(!entry.observation) text+="\n未验证";
    else {
        text+="\n"+entry.observation->Status()+"\n"+entry.observation->observed_at+" / "+entry.observation->run_id+
            "\n"+entry.observation->evidence_scope+"\n覆盖："+entry.observation->frontend_rule_coverage;
        for(const auto& warning:entry.observation->warnings) text+="\n"+warning;
    }
    details_->ChangeValue(WxUtf8(text));
    if(entry.observation) {
        const auto alive=alive_;
        const auto latest=detail_generation_;
        try {
            runtime_.BackgroundJobs().Submit([this,alive,latest,entry,text,generation](std::stop_token token) {
                if(token.stop_requested() || !alive->load() || latest->load()!=generation) return;
                std::string result;
                try { result=entry.observation->Status(FileSha256(PathFromUtf8(entry.config.model_path))); }
                catch(const std::exception& ex) { result="当前资源不可读取："+std::string(ex.what()); }
                if(token.stop_requested() || !alive->load() || latest->load()!=generation) return;
                wxTheApp->CallAfter([this,alive,latest,text,result,generation] {
                    if(alive->load() && latest->load()==generation) details_->ChangeValue(WxUtf8(text+"\n"+result));
                });
            });
        } catch(const std::exception& ex) {
            details_->ChangeValue(WxUtf8(text+"\n观察校验未启动："+std::string(ex.what())));
        }
    }
}
}
