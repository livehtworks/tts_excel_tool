#include "ui/ComparePanel.h"

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace adayo::ui {
ComparePanel::ComparePanel(wxWindow* parent) : wxPanel(parent) {
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto add_file_row = [&](const char* label, wxTextCtrl*& target) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(this, wxID_ANY, wxString::FromUTF8(label)), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        target = new wxTextCtrl(this, wxID_ANY);
        row->Add(target, 1, wxRIGHT, 8);
        row->Add(new wxButton(this, wxID_ANY, "打开"), 0);
        root->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);
    };
    add_file_row("正式文本:", reference_path_);
    add_file_row("机器文本:", actual_path_);

    auto* thresholds = new wxBoxSizer(wxHORIZONTAL);
    align_threshold_ = new wxSpinCtrlDouble(this, wxID_ANY); align_threshold_->SetRange(0, 100); align_threshold_->SetValue(80); align_threshold_->SetIncrement(1);
    pass_threshold_ = new wxSpinCtrlDouble(this, wxID_ANY); pass_threshold_->SetRange(0, 100); pass_threshold_->SetValue(100); pass_threshold_->SetIncrement(1);
    thresholds->Add(new wxStaticText(this, wxID_ANY, "对齐阈值"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    thresholds->Add(align_threshold_, 0, wxRIGHT, 18);
    thresholds->Add(new wxStaticText(this, wxID_ANY, "OK阈值"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    thresholds->Add(pass_threshold_, 0, wxRIGHT, 18);
    thresholds->Add(new wxButton(this, wxID_ANY, "自动对齐"), 0, wxRIGHT, 8);
    thresholds->Add(new wxButton(this, wxID_ANY, "导出 Excel"), 0);
    root->Add(thresholds, 0, wxALL, 10);

    root->Add(new wxStaticText(this, wxID_ANY,
        "核心已实现：Unicode code point 对比、归一化、锚点+顺序约束对齐、MISSING/EXTRA、字符级 Diff。P7 将 Diff 片段以 Excel 富文本标红。"),
        0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
    root->AddStretchSpacer(1);
    SetSizer(root);
}
} // namespace adayo::ui
