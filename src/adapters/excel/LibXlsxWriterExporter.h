#pragma once

#include "adapters/excel/IExcelExporter.h"

namespace adayo {
class LibXlsxWriterExporter final : public IExcelExporter {
public:
    void ExportRuntimeView(const RuntimeView& view, const std::filesystem::path& output) override;
    void ExportComparison(const std::vector<CompareRow>& rows, const std::filesystem::path& output) override;
};
} // namespace adayo
