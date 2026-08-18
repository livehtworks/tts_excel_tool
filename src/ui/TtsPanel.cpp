#include "ui/TtsPanel.h"

#include "core/workbook/ColumnAnalyzer.h"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace adayo::ui {
TtsPanel::TtsPanel(wxWindow* parent) : wxPanel(parent) {
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* file_row = new wxBoxSizer(wxHORIZONTAL);
    workbook_path_ = new wxTextCtrl(this, wxID_ANY);
    file_row->Add(new wxStaticText(this, wxID_ANY, "Excel:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    file_row->Add(workbook_path_, 1, wxRIGHT, 8);
    file_row->Add(new wxButton(this, wxID_ANY, "打开"), 0);
    root->Add(file_row, 0, wxEXPAND | wxALL, 10);

    auto* settings = new wxBoxSizer(wxHORIZONTAL);
    sheet_choice_ = new wxChoice(this, wxID_ANY);
    language_choice_ = new wxChoice(this, wxID_ANY);
    for (const auto& lang : ColumnAnalyzer::Languages()) language_choice_->Append(wxString::FromUTF8(lang.name));
    voice_choice_ = new wxChoice(this, wxID_ANY);
    speed_ = new wxSpinCtrlDouble(this, wxID_ANY);
    speed_->SetRange(0.5, 2.0); speed_->SetIncrement(0.1); speed_->SetValue(1.0);
    settings->Add(new wxStaticText(this, wxID_ANY, "Sheet"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    settings->Add(sheet_choice_, 1, wxRIGHT, 10);
    settings->Add(new wxStaticText(this, wxID_ANY, "语言"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    settings->Add(language_choice_, 1, wxRIGHT, 10);
    settings->Add(new wxStaticText(this, wxID_ANY, "Voice"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    settings->Add(voice_choice_, 1, wxRIGHT, 10);
    settings->Add(new wxStaticText(this, wxID_ANY, "语速"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    settings->Add(speed_, 0);
    root->Add(settings, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    text_input_ = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
    root->Add(text_input_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    auto* controls = new wxBoxSizer(wxHORIZONTAL);
    for (const char* label : {"生成/播放", "暂停", "继续", "停止", "保存 WAV"}) {
        controls->Add(new wxButton(this, wxID_ANY, wxString::FromUTF8(label)), 0, wxRIGHT, 8);
    }
    root->Add(controls, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

    root->Add(new wxStaticText(this, wxID_ANY,
        "骨架已保留旧版 Excel→列映射→运行视图→单句/顺序播放业务；P2/P4/P5 完成后接入实际数据表与播放状态机。"),
        0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
    SetSizer(root);
}
} // namespace adayo::ui
