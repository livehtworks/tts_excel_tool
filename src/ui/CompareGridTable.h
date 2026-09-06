#pragma once

#include "core/domain/Types.h"

#include <memory>
#include <vector>

#include <wx/grid.h>

namespace adayo::ui {

class CompareGridTable final : public wxGridTableBase {
public:
    explicit CompareGridTable(std::shared_ptr<const std::vector<CompareReportGroup>> reports);

    int GetNumberRows() override;
    int GetNumberCols() override;
    wxString GetValue(int row, int col) override;
    void SetValue(int row, int col, const wxString& value) override;
    wxString GetColLabelValue(int col) override;
    void SetReports(std::shared_ptr<const std::vector<CompareReportGroup>> reports);
    void SetCurrentGroup(std::size_t group);
    std::size_t CurrentGroup() const noexcept { return current_group_; }

private:
    std::shared_ptr<const std::vector<CompareReportGroup>> reports_;
    std::size_t current_group_{};
};

} // namespace adayo::ui
