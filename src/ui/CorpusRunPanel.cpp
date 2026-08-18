#include "ui/CorpusRunPanel.h"

#include "adapters/excel/LibXlsxWriterExporter.h"

#include <filesystem>

#include <wx/button.h>
#include <wx/filedlg.h>
#include <wx/grid.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace adayo::ui {
namespace {
wxString FromUtf8(const std::string& text) {
    return wxString::FromUTF8(text);
}

std::string ToUtf8(const wxString& text) {
    return text.ToUTF8().data() ? std::string(text.ToUTF8().data()) : std::string{};
}
} // namespace

CorpusRunPanel::CorpusRunPanel(wxWindow* parent) : wxPanel(parent) {
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* top = new wxBoxSizer(wxHORIZONTAL);
    status_ = new wxStaticText(this, wxID_ANY, "尚未生成运行视图");
    auto* export_button = new wxButton(this, wxID_ANY, "导出 Excel");
    export_button->Bind(wxEVT_BUTTON, &CorpusRunPanel::OnExport, this);
    top->Add(status_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    top->Add(export_button, 0);
    root->Add(top, 0, wxEXPAND | wxALL, 6);

    grid_ = new wxGrid(this, wxID_ANY);
    grid_->CreateGrid(0, 0);
    grid_->EnableEditing(true);
    grid_->Bind(wxEVT_GRID_CELL_CHANGED, &CorpusRunPanel::OnCellChanged, this);
    grid_->Bind(wxEVT_GRID_CELL_LEFT_DCLICK, &CorpusRunPanel::OnCellDClick, this);
    root->Add(grid_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    SetSizer(root);
}

void CorpusRunPanel::OnExport(wxCommandEvent&) {
    if (!session_) return;
    wxFileDialog dialog(this, "导出运行视图", "", "runtime.xlsx", "Excel workbook (*.xlsx)|*.xlsx", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() == wxID_OK) {
        try {
            LibXlsxWriterExporter exporter;
            exporter.ExportRuntimeView(session_->view, std::filesystem::path(dialog.GetPath().ToStdWstring()));
            status_->SetLabel("运行视图已导出");
        } catch (const std::exception& ex) {
            status_->SetLabel(FromUtf8(ex.what()));
        }
    }
}

void CorpusRunPanel::SetSession(CorpusSession session) {
    session_ = std::move(session);
    RefreshGrid();
}

void CorpusRunPanel::RefreshGrid() {
    grid_->Freeze();
    if (grid_->GetNumberRows() > 0) {
        grid_->DeleteRows(0, grid_->GetNumberRows());
    }
    if (grid_->GetNumberCols() > 0) {
        grid_->DeleteCols(0, grid_->GetNumberCols());
    }

    if (!session_) {
        status_->SetLabel("尚未生成运行视图");
        grid_->Thaw();
        return;
    }

    const auto& view = session_->view;
    if (!view.headers.empty()) {
        grid_->AppendCols(static_cast<int>(view.headers.size()));
        for (std::size_t c = 0; c < view.headers.size(); ++c) {
            grid_->SetColLabelValue(static_cast<int>(c), FromUtf8(view.headers[c]));
        }
    }
    if (!view.rows.empty()) {
        grid_->AppendRows(static_cast<int>(view.rows.size()));
        for (std::size_t r = 0; r < view.rows.size(); ++r) {
            for (std::size_t c = 0; c < view.headers.size(); ++c) {
                const std::string value = c < view.rows[r].size() ? view.rows[r][c] : std::string{};
                grid_->SetCellValue(static_cast<int>(r), static_cast<int>(c), FromUtf8(value));
                if (c < view.columns.size() && view.columns[c].role == ColumnRole::Index) {
                    grid_->SetReadOnly(static_cast<int>(r), static_cast<int>(c));
                }
            }
        }
    }
    grid_->AutoSizeColumns(false);
    status_->SetLabel(wxString::Format("运行视图：%d 行，%d 列。双击结果列循环 blank/OK/NG。", grid_->GetNumberRows(), grid_->GetNumberCols()));
    grid_->Thaw();
}

void CorpusRunPanel::OnCellChanged(wxGridEvent& event) {
    if (!session_) {
        event.Skip();
        return;
    }
    try {
        const auto row = static_cast<std::size_t>(event.GetRow());
        const auto col = static_cast<std::size_t>(event.GetCol());
        service_.UpdateDisplayCell(*session_, row, col, ToUtf8(grid_->GetCellValue(event.GetRow(), event.GetCol())));
        RefreshGrid();
    } catch (const std::exception& ex) {
        status_->SetLabel(FromUtf8(ex.what()));
    }
}

void CorpusRunPanel::OnCellDClick(wxGridEvent& event) {
    if (!session_) {
        event.Skip();
        return;
    }
    try {
        const auto row = static_cast<std::size_t>(event.GetRow());
        const auto col = static_cast<std::size_t>(event.GetCol());
        if (col < session_->view.columns.size() && session_->view.columns[col].role == ColumnRole::Result) {
            service_.CycleResult(*session_, row, col);
            RefreshGrid();
            return;
        }
    } catch (const std::exception& ex) {
        status_->SetLabel(FromUtf8(ex.what()));
    }
    event.Skip();
}

} // namespace adayo::ui
