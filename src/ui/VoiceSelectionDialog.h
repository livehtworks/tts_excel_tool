#pragma once

#include "services/ModelRegistry.h"
#include <optional>
#include <atomic>
#include <memory>
#include <wx/dialog.h>

class wxListBox;
class wxTextCtrl;
class wxButton;
namespace adayo { class ApplicationRuntime; }

namespace adayo::ui {
class VoiceSelectionDialog final : public wxDialog {
public:
    VoiceSelectionDialog(wxWindow* parent, ApplicationRuntime& runtime,
        const std::vector<TtsModelEntry>& entries, std::string locale, std::string current_id);
    std::string SelectedId() const;
    ~VoiceSelectionDialog() override { alive_->store(false); }
private:
    struct Group { std::filesystem::path root; std::string name; std::vector<std::size_t> entries; };
    void Filter();
    void ShowSpeakers();
    void ShowDetails();
    bool Matches(const TtsModelEntry& entry) const;
    ApplicationRuntime& runtime_;
    const std::vector<TtsModelEntry>& entries_;
    std::vector<Group> groups_;
    std::vector<std::size_t> shown_groups_, shown_entries_;
    std::string locale_, current_id_;
    wxTextCtrl* search_{};
    wxListBox* models_{};
    wxListBox* speakers_{};
    wxTextCtrl* details_{};
    wxButton* confirm_{};
    std::shared_ptr<std::atomic_uint64_t> detail_generation_=std::make_shared<std::atomic_uint64_t>(0);
    std::shared_ptr<std::atomic_bool> alive_=std::make_shared<std::atomic_bool>(true);
};
}
