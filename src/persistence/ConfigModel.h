#pragma once

#include "core/domain/Types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace adayo {
struct SheetHeaderRowConfig {
    std::string workbook_identity;
    std::string sheet_name;
    std::size_t header_row{1};
};

struct SheetMappingConfig {
    std::string workbook_identity;
    std::string sheet_name;
    std::size_t header_row{1};
    std::vector<ColumnProfile> columns;
};

struct AppConfig {
    int schema_version{3};
    std::string last_workbook;
    std::string last_sheet;
    double speech_rate{1.0};
    double alignment_threshold{80.0};
    double pass_threshold{100.0};
    std::vector<SheetHeaderRowConfig> sheet_header_rows;
    std::vector<SheetMappingConfig> sheet_mappings;
};
} // namespace adayo
