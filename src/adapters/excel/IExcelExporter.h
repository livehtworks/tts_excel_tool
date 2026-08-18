#pragma once

#include "core/domain/Types.h"

#include <filesystem>
#include <vector>

namespace adayo {
class IExcelExporter {
public:
    virtual ~IExcelExporter() = default;
    virtual void ExportRuntimeView(const RuntimeView& view, const std::filesystem::path& output) = 0;
    virtual void ExportComparison(const std::vector<CompareRow>& rows, const std::filesystem::path& output) = 0;
    virtual void ExportComparisonGroups(const std::vector<CompareReportGroup>& groups, const std::filesystem::path& output) = 0;
};
} // namespace adayo
