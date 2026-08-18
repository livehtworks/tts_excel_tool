#include "adapters/excel/OpenXlsxWorkbookReader.h"
#include "core/workbook/ColumnAnalyzer.h"
#include "core/workbook/ViewBuilder.h"
#include "persistence/JsonConfigStore.h"
#include "services/CorpusViewService.h"
#include "services/ModelRegistry.h"
#include "services/WorkbookService.h"

#include "TestCheck.h"

#include <filesystem>
#include <fstream>
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
        profile.tts_model_id,
    };
}

void TestWorkbookReadAndAnalyze() {
    OpenXlsxWorkbookReader reader;
    const auto names = reader.SheetNames(FixturePath());
    REQUIRE((names == std::vector<std::string>{"Vehicle", "System", "EmptySheet"}));

    const auto vehicle = reader.ReadSheet(FixturePath(), "Vehicle", 2);
    REQUIRE(vehicle.header_row == 2);
    REQUIRE(vehicle.rows.size() == 4);
    REQUIRE((vehicle.headers == std::vector<std::string>{"序号", "二级功能", "三级功能", "示例Query", "ENG", "ENU", "FRF", "ARG", "SPM", "ENG结果"}));

    ColumnAnalyzer analyzer;
    const auto profiles = analyzer.Analyze(vehicle.headers, vehicle.rows);
    REQUIRE(profiles.size() == vehicle.headers.size());

    const auto& serial = FindColumn(profiles, "序号");
    REQUIRE(serial.excel_column == "A");
    REQUIRE(serial.suggested_type == SuggestedColumnType::Unknown);

    const auto& level3 = FindColumn(profiles, "三级功能");
    REQUIRE(level3.excel_column == "C");
    REQUIRE(level3.suggested_type == SuggestedColumnType::Meta);
    REQUIRE(!level3.selected);
    REQUIRE(level3.role == ColumnRole::Ignore);

    const auto& eng = FindColumn(profiles, "ENG");
    REQUIRE(eng.excel_column == "E");
    REQUIRE(eng.suggested_type == SuggestedColumnType::Utterance);
    REQUIRE(eng.guessed_language == "en-GB");
    REQUIRE(eng.non_empty_count == 3);
    REQUIRE(eng.selected);
    REQUIRE(eng.role == ColumnRole::Play);

    const auto& enu = FindColumn(profiles, "ENU");
    REQUIRE(enu.guessed_language == "en-US");
    REQUIRE(enu.non_empty_count == 2);

    REQUIRE(FindColumn(profiles, "FRF").guessed_language == "fr-FR");
    REQUIRE(FindColumn(profiles, "ARG").guessed_language == "ar-SA");
    REQUIRE(FindColumn(profiles, "SPM").guessed_language == "es-ES");

    const auto& eng_result = FindColumn(profiles, "ENG结果");
    REQUIRE(eng_result.suggested_type == SuggestedColumnType::Result);
    REQUIRE(eng_result.guessed_language == "en-GB");
    REQUIRE(!eng_result.selected);
    REQUIRE(eng_result.role == ColumnRole::Ignore);
}

void TestWorkbookReadFromChinesePath() {
    const auto dir = std::filesystem::temp_directory_path() / "adayo_中文路径读取验证";
    std::filesystem::create_directories(dir);
    const auto copied = dir / "业务样本.xlsx";
    std::filesystem::copy_file(FixturePath(), copied, std::filesystem::copy_options::overwrite_existing);

    OpenXlsxWorkbookReader reader;
    const auto data = reader.ReadSheet(copied, "Vehicle", 2);
    REQUIRE(data.headers.size() == 10);
    REQUIRE(data.rows.size() == 4);
    REQUIRE(data.rows[0][4] == "Turn on feature A\nEnable feature A");

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
    REQUIRE((result.view.headers == std::vector<std::string>{"序号", "三级功能", "ENG", "ENG结果", "ENU", "ENU结果"}));
    REQUIRE(result.view.rows.size() == 8);
    REQUIRE((result.view.rows[0] == std::vector<std::string>{"1", "开关控制", "Turn on feature A", "", "Turn on feature A", ""}));
    REQUIRE((result.view.rows[1] == std::vector<std::string>{"2", "开关控制", "Enable feature A", "", "", ""}));
    REQUIRE((result.view.rows[7] == std::vector<std::string>{"8", "空单元格验证", "", "", "", ""}));
    REQUIRE(result.view.row_meta[1].raw_row_index == 0);
    REQUIRE(result.view.row_meta[1].segment_indexes.at(4).value() == 1);
    REQUIRE(!result.view.row_meta[1].segment_indexes.at(5).has_value());
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
    auto saved_profiles = profiles;
    saved_profiles[1].tts_model_id = "vits-piper-en_US-amy-low";
    saved_profiles[1].language_user_overridden = true;
    config.sheet_mappings.push_back({"fixture:vehicle", "Vehicle", 2, saved_profiles});
    config.sheet_header_rows.push_back({"fixture:vehicle", "Vehicle", 2});

    const auto path = std::filesystem::temp_directory_path() / "adayo_config_store_test" / "config.json";
    JsonConfigStore store(path);
    store.Save(config);
    const auto loaded = store.Load();

    REQUIRE(loaded.last_workbook == config.last_workbook);
    REQUIRE(loaded.last_sheet == config.last_sheet);
    REQUIRE(loaded.schema_version == 2);
    REQUIRE(loaded.speech_rate == config.speech_rate);
    REQUIRE(loaded.sheet_header_rows.size() == 1);
    REQUIRE(loaded.sheet_header_rows[0].header_row == 2);
    REQUIRE(loaded.sheet_mappings.size() == 1);
    REQUIRE(loaded.sheet_mappings[0].header_row == 2);
    REQUIRE(loaded.sheet_mappings[0].columns.size() == 3);
    REQUIRE(loaded.sheet_mappings[0].columns[2].header == "ARG");
    REQUIRE(loaded.sheet_mappings[0].columns[2].language_code == "ar-SA");
    REQUIRE(loaded.sheet_mappings[0].columns[1].tts_model_id == "vits-piper-en_US-amy-low");
    REQUIRE(loaded.sheet_mappings[0].columns[1].language_user_overridden);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void TestJsonConfigV1LanguageMigrationDoesNotOverrideAnalyzer() {
    const auto path = std::filesystem::temp_directory_path() / "adayo_config_store_test" / "config_v1.json";
    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output <<
            R"({
  "schema_version": 1,
  "sheet_mappings": [
    {
      "workbook_identity": "fixture:vehicle",
      "sheet_name": "Vehicle",
      "header_row": 2,
      "columns": [
        {
          "source_index": 7,
          "excel_column": "H",
          "header": "ARG",
          "selected": true,
          "role": "play",
          "language_code": "en-US",
          "tts_engine_id": "sherpa-vits"
        }
      ]
    }
  ]
})";
    }

    JsonConfigStore store(path);
    const auto loaded = store.Load();
    REQUIRE(loaded.schema_version == 2);
    REQUIRE(!loaded.sheet_mappings[0].columns[0].language_user_overridden);

    WorkbookService service(std::make_unique<OpenXlsxWorkbookReader>());
    auto analysis = service.AnalyzeSheet(FixturePath(), "Vehicle", 2, loaded);
    REQUIRE(FindColumn(analysis.columns, "ARG").language_code == "ar-SA");

    auto config = loaded;
    config.sheet_mappings[0].workbook_identity = analysis.identity;
    config.sheet_mappings[0].columns[0].language_user_overridden = true;
    WorkbookService::UpsertMapping(config, config.sheet_mappings[0]);
    analysis = service.AnalyzeSheet(FixturePath(), "Vehicle", 2, config);
    REQUIRE(FindColumn(analysis.columns, "ARG").language_code == "en-US");

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
    REQUIRE(found.has_value());
    REQUIRE(found->columns[0].header == "序号");
    const auto missing = WorkbookService::FindMapping(config, "fixture", "Vehicle", 3);
    REQUIRE(!missing.has_value());
}

void TestWorkbookIdentityIgnoresContentVersion() {
    const auto path = std::filesystem::temp_directory_path() / "adayo_identity_version_test.xlsx";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "first";
    }
    const auto first = WorkbookService::WorkbookIdentity(path);
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "second version with different size";
    }
    const auto second = WorkbookService::WorkbookIdentity(path);
    REQUIRE(first == second);

    AppConfig config;
    WorkbookService::UpsertHeaderRow(config, first, "Vehicle", 2);
    REQUIRE(WorkbookService::FindHeaderRow(config, second, "Vehicle").value() == 2);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void TestSavedLanguageOverrideSemantics() {
    WorkbookService service(std::make_unique<OpenXlsxWorkbookReader>());
    AppConfig config;
    const auto identity = WorkbookService::WorkbookIdentity(FixturePath());
    ColumnProfile saved;
    saved.source_index = 7;
    saved.excel_column = "H";
    saved.header = "ARG";
    saved.selected = true;
    saved.role = ColumnRole::Play;
    saved.language_code = "en-US";
    saved.language_user_overridden = false;
    saved.tts_engine_id = "sherpa-vits";
    saved.tts_model_id = "voice-a";
    WorkbookService::UpsertMapping(config, {identity, "Vehicle", 2, {saved}});

    auto analysis = service.AnalyzeSheet(FixturePath(), "Vehicle", 2, config);
    const auto& inferred = FindColumn(analysis.columns, "ARG");
    REQUIRE(inferred.language_code == "ar-SA");
    REQUIRE(inferred.tts_model_id == "voice-a");

    config.sheet_mappings[0].columns[0].language_user_overridden = true;
    analysis = service.AnalyzeSheet(FixturePath(), "Vehicle", 2, config);
    REQUIRE(FindColumn(analysis.columns, "ARG").language_code == "en-US");
}

void TestModelRegistryKeepsValidModelsWhenOneIsBroken() {
    const auto root = std::filesystem::temp_directory_path() / "adayo_model_registry_test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "good" / "espeak-ng-data");
    std::filesystem::create_directories(root / "broken");
    std::filesystem::create_directories(root / "missing-json");
    {
        std::ofstream(root / "good" / "model.onnx").put('\0');
        std::ofstream(root / "good" / "tokens.txt").put('\0');
        std::ofstream config_file(root / "good" / "model.json", std::ios::binary | std::ios::trunc);
        config_file <<
            R"({"id":"good","display_name":"Good","engine_id":"sherpa-vits","model":"model.onnx","tokens":"tokens.txt","data_dir":"espeak-ng-data","language_code":"en-US"})";
    }
    {
        std::ofstream config_file(root / "broken" / "model.json", std::ios::binary | std::ios::trunc);
        config_file << R"({"id":"broken","model":"missing.onnx","tokens":"tokens.txt"})";
    }

    ModelRegistry registry(root);
    const auto scan = registry.ScanSherpaModelsWithDiagnostics();
    REQUIRE(scan.entries.size() == 1);
    REQUIRE(scan.entries[0].id == "good");
    REQUIRE(scan.invalid.size() == 2);

    std::filesystem::remove_all(root, ec);
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
    REQUIRE(session.view.rows.size() == 8);

    service.UpdateDisplayCell(session, 1, 2, "Enable feature A updated");
    REQUIRE(session.source_rows[0][4] == "Turn on feature A\nEnable feature A updated");
    REQUIRE(session.source_rows[0][5] == "Turn on feature A");
    REQUIRE(session.view.rows[0][2] == "Turn on feature A");
    REQUIRE(session.view.rows[1][2] == "Enable feature A updated");

    REQUIRE(service.CycleResult(session, 0, 3) == ResultCycleState::Ok);
    REQUIRE(session.view.rows[0][3] == "✔");
    REQUIRE(service.CycleResult(session, 0, 3) == ResultCycleState::Ng);
    REQUIRE(session.view.rows[0][3] == "×");
    REQUIRE(service.CycleResult(session, 0, 3) == ResultCycleState::Blank);
    REQUIRE(session.view.rows[0][3].empty());
}

void TestResultIdentitySurvivesEarlierRowStructureEdit() {
    std::vector<std::vector<std::string>> rows = {
        {"ref-a", "a1\na2\na3", "b1"},
        {"ref-b", "target", "other"},
    };
    std::vector<SelectedColumn> columns = {
        {0, "REF", "A", ColumnRole::Reference, "", "", ""},
        {1, "ENG", "B", ColumnRole::Play, "en-US", "sherpa-vits", "voice-en"},
        {2, "ENU", "C", ColumnRole::Play, "en-US", "sherpa-vits", "voice-us"},
    };

    CorpusViewService service;
    auto session = service.CreateSession(rows, columns);
    REQUIRE(service.CycleResult(session, 3, 3) == ResultCycleState::Ok);
    REQUIRE(session.view.rows[3][3] == "✔");

    service.UpdateDisplayCell(session, 0, 2, "");
    REQUIRE(session.view.rows[2][3] == "✔");
    REQUIRE(session.view.row_meta[2].raw_row_index == 1);
}

void TestSyntheticBlankCannotBeEditedOrMarked() {
    std::vector<std::vector<std::string>> rows = {
        {"ref-a", "a1\na2\na3", "b1"},
    };
    std::vector<SelectedColumn> columns = {
        {0, "REF", "A", ColumnRole::Reference, "", "", ""},
        {1, "ENG", "B", ColumnRole::Play, "en-US", "sherpa-vits", "voice-en"},
        {2, "ENU", "C", ColumnRole::Play, "en-US", "sherpa-vits", "voice-us"},
    };

    CorpusViewService service;
    auto session = service.CreateSession(rows, columns);
    bool edit_threw = false;
    try {
        service.UpdateDisplayCell(session, 2, 4, "should not append second segment");
    } catch (const std::invalid_argument&) {
        edit_threw = true;
    }
    REQUIRE(edit_threw);
    REQUIRE(session.source_rows[0][2] == "b1");

    bool result_threw = false;
    try {
        (void)service.CycleResult(session, 2, 5);
    } catch (const std::invalid_argument&) {
        result_threw = true;
    }
    REQUIRE(result_threw);
}
} // namespace

int main() {
    return test::RunTestMain("adayo_p2_tests", [] {
        TestWorkbookReadAndAnalyze();
        TestWorkbookReadFromChinesePath();
        TestRuntimeViewFromFixture();
        TestJsonConfigStoreRoundTrip();
        TestJsonConfigV1LanguageMigrationDoesNotOverrideAnalyzer();
        TestWorkbookMappingIdentityIncludesHeaderRow();
        TestWorkbookIdentityIgnoresContentVersion();
        TestSavedLanguageOverrideSemantics();
        TestModelRegistryKeepsValidModelsWhenOneIsBroken();
        TestCorpusViewEditAndResultCycle();
        TestResultIdentitySurvivesEarlierRowStructureEdit();
        TestSyntheticBlankCannotBeEditedOrMarked();
    });
}
