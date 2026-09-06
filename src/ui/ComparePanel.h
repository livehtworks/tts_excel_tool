#pragma once
#include "core/domain/Types.h"
#include "services/CompareService.h"

#include <filesystem>
#include <cstdint>
#include <memory>
#include <optional>

#include <wx/panel.h>
#include <wx/collpane.h>

class wxButton;
class wxCheckBox;
class wxChoice;
class wxCommandEvent;
class wxGrid;
class wxListBox;
class wxSpinCtrlDouble;
class wxStaticText;
class wxTextCtrl;
class wxRichTextCtrl;

namespace adayo {
class ApplicationRuntime;
}

namespace adayo::ui {
class CompareGridTable;

class ComparePanel final : public wxPanel {
public:
    ComparePanel(wxWindow* parent, ApplicationRuntime& runtime);
    void BeginShutdown();
private:
    struct CompareInputGroup {
        std::uint64_t id{};
        std::string label;
        std::filesystem::path reference_path;
        std::filesystem::path actual_path;
    };

    void OnOpenReference(wxCommandEvent& event);
    void LayoutOptions();
    void OnOpenActual(wxCommandEvent& event);
    void OnAddGroup(wxCommandEvent& event);
    void OnRemoveGroup(wxCommandEvent& event);
    void OnCompare(wxCommandEvent& event);
    void OnExport(wxCommandEvent& event);
    void OnInputChanged(wxCommandEvent& event);
    bool ApplyGroupDraft();
    bool ResolveGroupDraft();
    void LoadGroupDraft(std::optional<std::uint64_t> id);
    void UpdateDraftState();
    void SelectInputGroup();
    bool CommitOptions();
    void ObserveOptions();
    void ShowDiffDetails(int row);

    void MarkInputChanged();
    void InvalidateReport();
    void LoadFileInto(wxTextCtrl* target);
    std::vector<CompareInputGroup> CurrentInputGroups() const;
    CompareOptions CurrentOptions() const;
    void ApplyOptions(const CompareOptions& options);
    void RefreshOptionState();
    std::string CurrentDelimiter() const;
    void RefreshGroups();
    void RefreshGrid();
    void SetBusy(bool busy, const wxString& message);
    void SetStatus(const wxString& message);

    ApplicationRuntime& runtime_;
    wxTextCtrl* language_label_{};
    wxTextCtrl* reference_path_{};
    wxTextCtrl* actual_path_{};
    wxTextCtrl* delimiter_{};
    wxSpinCtrlDouble* align_threshold_{};
    wxSpinCtrlDouble* pass_threshold_{};
    wxCheckBox* ignore_punctuation_{};
    wxChoice* profile_{};
    wxChoice* pairing_{};
    wxChoice* normalization_{};
    wxCheckBox* case_fold_{};
    wxCheckBox* collapse_whitespace_{};
    wxCheckBox* trim_{};
    wxSpinCtrlDouble* anchor_threshold_{};
    wxSpinCtrlDouble* anchor_margin_{};
    wxSpinCtrlDouble* gap_penalty_{};
    wxTextCtrl* max_error_{};
    wxStaticText* option_summary_{};
    std::vector<wxWindow*> option_controls_;
    bool applying_options_{false};
    bool comparing_{false};
    bool filling_draft_{false}, draft_dirty_{false}, options_valid_{true};
    std::uint64_t next_group_id_{};
    std::optional<std::uint64_t> editing_group_id_;
    std::optional<CompareOptions> observed_options_;
    wxButton* new_group_{};
    wxButton* apply_group_{};
    wxButton* cancel_group_{};
    wxCollapsiblePane* advanced_{};
    wxChoice* result_group_{};
    wxStaticText* result_summary_{};
    wxRichTextCtrl* reference_detail_{};
    wxRichTextCtrl* actual_detail_{};
    wxStaticText* reference_detail_label_{};
    wxStaticText* actual_detail_label_{};
    wxButton* add_group_{};
    wxButton* remove_group_{};
    wxButton* reference_browse_{};
    wxButton* actual_browse_{};
    wxButton* compare_button_{};
    wxButton* cancel_button_{};
    std::stop_source comparison_cancel_;
    wxButton* export_button_{};
    wxStaticText* status_{};
    wxListBox* group_list_{};
    wxGrid* result_grid_{};
    CompareGridTable* result_table_{};
    std::vector<CompareInputGroup> input_groups_;
    std::shared_ptr<const std::vector<CompareReportGroup>> report_groups_;
    std::uint64_t input_revision_{0};
    std::uint64_t report_revision_{static_cast<std::uint64_t>(-1)};
    bool busy_{false};
    bool closing_{false};
};
} // namespace adayo::ui
