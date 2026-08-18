#include "adapters/excel/OpenXlsxWorkbookReader.h"
#include "core/workbook/ColumnAnalyzer.h"
#include "core/workbook/ViewBuilder.h"
#include "persistence/JsonConfigStore.h"
#include "services/CorpusViewService.h"
#include "services/WorkbookService.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace adayo;

namespace {
std::filesystem::path FixturePath() {
    return std::filesystem::path(ADAYO_FIXTURE_DIR) / "adayo_legacy_business_fixture.xlsx";
}

const ColumnProfile& FindColumn(const std::vector<ColumnProfile>& columns, const std::string& header) {
    for (const auto& column : columns) {
        if (column.header == header) return column;
    }
    throw std::runtime_error("Missing column profile: " + header);
}

SelectedColumn ToSelected(const ColumnProfile& profile, ColumnRole role) {
    return {
        profile.source_index,
        profile.header,
        profile.excel_column,
        role,
        profile.language_code,
        profile.tts_engine_id,
    };
}

void TestWorkbookReadAndAnalyze() {
    OpenXlsxWorkbookReader reader;
    const auto names = reader.SheetNames(FixturePath());
    assert((names == std::vector<std::string>{"Vehicle", "System", "EmptySheet"}));

    const auto vehicle = reader.ReadSheet(FixturePath(), "Vehicle", 2);
    assert(vehicle.header_row == 2);
    assert(vehicle.rows.size() == 4);
    assert((vehicle.headers == std::vector<std::string>{"序号", "二级功能", "三级功能", "示例Query", "ENG", "ENU", "FRF", "ARG", "SPM", "ENG结果"}));

    ColumnAnalyzer analyzer;
    const auto profiles = analyzer.Analyze(vehicle.headers, vehicle.rows);
    assert(profiles.size() == vehicle.headers.size());

    const auto& serial = FindColumn(profiles, "序号");
    assert(serial.excel_column == "A");
    assert(serial.suggested_type == SuggestedColumnType::Unknown);

    const auto& level3 = FindColumn(profiles, "三级功能");
    assert(level3.excel_column == "C");
    assert(level3.suggested_type == SuggestedColumnType::Meta);

    const auto& eng = FindColumn(profiles, "ENG");
    assert(eng.excel_column == "E");
    assert(eng.suggested_type == SuggestedColumnType::Utterance);
    assert(eng.guessed_language == "en-GB");
    assert(eng.non_empty_count == 3);

    const auto& enu = FindColumn(profiles, "ENU");
    assert(enu.guessed_language == "en-US");
    assert(enu.non_empty_count == 3);

    assert(FindColumn(profiles, "FRF").guessed_language == "fr-FR");
    assert(FindColumn(profiles, "ARG").guessed_language == "ar-SA");
    assert(FindColumn(profiles, "SPM").guessed_language == "es-ES");

    const auto& eng_result = FindColumn(profiles, "ENG结果");
    assert(eng_result.suggested_type == SuggestedColumnType::Result);
    assert(eng_result.guessed_language == "en-GB");
}

void TestWorkbookReadFromChinesePath() {
    const auto dir = std::filesystem::temp_directory_path() / "adayo_中文路径读取验证";
    std::filesystem::create_directories(dir);
    const auto copied = dir / "业务样本.xlsx";
    std::filesystem::copy_file(FixturePath(), copied, std::filesystem::copy_options::overwrite_existing);

    OpenXlsxWorkbookReader reader;
    const auto data = reader.ReadSheet(copied, "Vehicle", 2);
    assert(data.headers.size() == 10);
    assert(data.rows.size() == 4);
    assert(data.rows[0][4] == "Turn on feature A\nEnable feature A");

    std::error_code ec;
    std::filesystem::remove(copied, ec);
    std::filesystem::remove(dir, ec);
}

void TestRuntimeViewFromFixture() {
    OpenXlsxWorkbookReader reader;
    const auto vehicle = reader.ReadSheet(FixturePath(), "Vehicle", 2);

    ColumnAnalyzer analyzer;
    const auto profiles = analyzer.Analyze(vehicle.headers, vehicle.rows);
    std::vector<SelectedColumn> columns = {
        ToSelected(FindColumn(profiles, "三级功能"), ColumnRole::Reference),
        ToSelected(FindColumn(profiles, "ENG"), ColumnRole::Play),
        ToSelected(FindColumn(profiles, "ENU"), ColumnRole::Play),
    };

    ViewBuilder builder;
    const auto result = builder.Build(vehicle.rows, columns);
    assert((result.view.headers == std::vector<std::string>{"序号", "三级功能", "ENG", "ENG结果", "ENU", "ENU结果"}));
    assert(result.view.rows.size() == 8);
    assert((result.view.rows[0] == std::vector<std::string>{"1", "开关控制", "Turn on feature A", "", "Turn on feature A", ""}));
    assert((result.view.rows[1] == std::vector<std::string>{"2", "开关控制", "Enable feature A", "", "", ""}));
    assert((result.view.rows[7] == std::vector<std::string>{"8", "空单元格验证", "", "", "", ""}));
    assert(result.view.row_meta[1].raw_row_index == 0);
    assert(result.view.row_meta[1].segment_indexes.at(4).value() == 1);
    assert(!result.view.row_meta[1].segment_indexes.at(5).has_value());
}

void TestJsonConfigStoreRoundTrip() {
    ColumnAnalyzer analyzer;
    const auto profiles = analyzer.Analyze({"三级功能", "ENG", "ARG"}, {{"开关控制", "Turn on feature A", "تشغيل"}});

    AppConfig config;
    config.last_workbook = "adayo_legacy_business_fixture.xlsx";
    config.last_sheet = "Vehicle";
    config.speech_rate = 0.95;
    config.alignment_threshold = 82.0;
    config.pass_threshold = 99.0;
    config.sheet_mappings.push_back({"fixture:vehicle", "Vehicle", 2, profiles});

    const auto path = std::filesystem::temp_directory_path() / "adayo_config_store_test" / "config.json";
    JsonConfigStore store(path);
    store.Save(config);
    const auto loaded = store.Load();

    assert(loaded.last_workbook == config.last_workbook);
    assert(loaded.last_sheet == config.last_sheet);
    assert(loaded.speech_rate == config.speech_rate);
    assert(loaded.sheet_mappings.size() == 1);
    assert(loaded.sheet_mappings[0].header_row == 2);
    assert(loaded.sheet_mappings[0].columns.size() == 3);
    assert(loaded.sheet_mappings[0].columns[2].header == "ARG");
    assert(loaded.sheet_mappings[0].columns[2].language_code == "ar-SA");

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void TestWorkbookMappingIdentityIncludesHeaderRow() {
    AppConfig config;
    SheetMappingConfig row2;
    row2.workbook_identity = "fixture";
    row2.sheet_name = "Vehicle";
    row2.header_row = 2;
    row2.columns.push_back({0, "A", "序号"});

    SheetMappingConfig row1 = row2;
    row1.header_row = 1;
    row1.columns[0].header = "wrong";

    WorkbookService::UpsertMapping(config, row1);
    WorkbookService::UpsertMapping(config, row2);

    const auto found = WorkbookService::FindMapping(config, "fixture", "Vehicle", 2);
    assert(found.has_value());
    assert(found->columns[0].header == "序号");
    const auto missing = WorkbookService::FindMapping(config, "fixture", "Vehicle", 3);
    assert(!missing.has_value());
}

void TestCorpusViewEditAndResultCycle() {
    OpenXlsxWorkbookReader reader;
    const auto vehicle = reader.ReadSheet(FixturePath(), "Vehicle", 2);

    ColumnAnalyzer analyzer;
    const auto profiles = analyzer.Analyze(vehicle.headers, vehicle.rows);
    std::vector<SelectedColumn> columns = {
        ToSelected(FindColumn(profiles, "三级功能"), ColumnRole::Reference),
        ToSelected(FindColumn(profiles, "ENG"), ColumnRole::Play),
        ToSelected(FindColumn(profiles, "ENU"), ColumnRole::Play),
    };

    CorpusViewService service;
    auto session = service.CreateSession(vehicle.rows, columns);
    assert(session.view.rows.size() == 8);

    service.UpdateDisplayCell(session, 1, 2, "Enable feature A updated");
    assert(session.source_rows[0][4] == "Turn on feature A\nEnable feature A updated");
    assert(session.source_rows[0][5] == "Turn on feature A");
    assert(session.view.rows[0][2] == "Turn on feature A");
    assert(session.view.rows[1][2] == "Enable feature A updated");

    assert(service.CycleResult(session, 0, 3) == ResultCycleState::Ok);
    assert(session.view.rows[0][3] == "✔");
    assert(service.CycleResult(session, 0, 3) == ResultCycleState::Ng);
    assert(session.view.rows[0][3] == "×");
    assert(service.CycleResult(session, 0, 3) == ResultCycleState::Blank);
    assert(session.view.rows[0][3].empty());
}
} // namespace

int main() {
    TestWorkbookReadAndAnalyze();
    TestWorkbookReadFromChinesePath();
    TestRuntimeViewFromFixture();
    TestJsonConfigStoreRoundTrip();
    TestWorkbookMappingIdentityIncludesHeaderRow();
    TestCorpusViewEditAndResultCycle();
    std::cout << "adayo_p2_tests: PASS\n";
    return 0;
}
