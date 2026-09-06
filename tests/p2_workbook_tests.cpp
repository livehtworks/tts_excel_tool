#include "adapters/excel/OpenXlsxWorkbookReader.h"
#include "core/workbook/ColumnAnalyzer.h"
#include "core/workbook/ViewBuilder.h"
#include "persistence/JsonConfigStore.h"
#include "services/CompareService.h"
#include "services/CorpusViewService.h"
#include "services/ModelRegistry.h"
#include "services/WorkbookService.h"
#include "platform/FileIo.h"
#include "platform/UnicodePath.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <map>
#include <memory>
#include <chrono>
#include <future>
#ifdef ADAYO_CAN_TEST_RUNTIME
#include "app/ApplicationRuntime.h"
#ifdef _WIN32
#include <windows.h>
#endif
#endif

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
    const auto dir = test::IsolatedRoot() / PathFromUtf8("adayo_中文路径读取验证");
    std::filesystem::create_directories(dir);
    const auto copied = dir / PathFromUtf8("业务样本.xlsx");
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
    saved_profiles[1].language_selection_mode = LanguageSelectionMode::Fixed;
    config.sheet_mappings.push_back({"fixture:vehicle", "Vehicle", 2, saved_profiles});
    config.sheet_header_rows.push_back({"fixture:vehicle", "Vehicle", 2});

    const auto path = test::IsolatedRoot() / "adayo_config_store_test" / "config.json";
    JsonConfigStore store(path);
    store.Save(config);
    const auto loaded = store.Load();

    REQUIRE(loaded.last_workbook == config.last_workbook);
    REQUIRE(loaded.last_sheet == config.last_sheet);
    REQUIRE(loaded.schema_version == 4);
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
    REQUIRE(loaded.sheet_mappings[0].columns[1].language_selection_mode == LanguageSelectionMode::Fixed);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void TestJsonConfigCorruptBackupAndSafeSave() {
    const auto dir = test::IsolatedRoot() / "adayo_config_store_corrupt_test";
    const auto path = dir / "config.json";
    std::filesystem::create_directories(dir);
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << R"({"schema_version":2,"last_workbook":)";
    }

    JsonConfigStore store(path);
    const auto loaded = store.LoadOrDefault();
    REQUIRE(loaded.status == ConfigLoadStatus::CorruptBackedUp);
    REQUIRE(loaded.backup_path.has_value());
    REQUIRE(std::filesystem::exists(*loaded.backup_path));
    REQUIRE(!std::filesystem::exists(path));
    REQUIRE(loaded.allow_save);

    AppConfig config;
    config.last_workbook = "after_corrupt.xlsx";
    store.Save(config);
    const auto saved = store.Load();
    REQUIRE(saved.last_workbook == "after_corrupt.xlsx");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

void TestJsonConfigFutureSchemaRefusesOverwrite() {
    const auto dir = test::IsolatedRoot() / "adayo_config_store_future_test";
    const auto path = dir / "config.json";
    std::filesystem::create_directories(dir);
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << R"({"schema_version":999,"last_workbook":"future.xlsx"})";
    }

    JsonConfigStore store(path);
    const auto loaded = store.LoadOrDefault();
    REQUIRE(loaded.status == ConfigLoadStatus::FutureSchema);
    REQUIRE(!loaded.allow_save);

    bool threw = false;
    try {
        AppConfig config;
        config.last_workbook = "must_not_overwrite.xlsx";
        store.Save(config);
    } catch (const std::exception&) {
        threw = true;
    }
    REQUIRE(threw);
    {
        std::ifstream input(path, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        REQUIRE(content.find("future.xlsx") != std::string::npos);
        REQUIRE(content.find("must_not_overwrite") == std::string::npos);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

void TestJsonConfigV1LanguageMigrationDoesNotOverrideAnalyzer() {
    const auto path = test::IsolatedRoot() / "adayo_config_store_test" / "config_v1.json";
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
    REQUIRE(loaded.schema_version == 4);
    REQUIRE(!loaded.sheet_mappings[0].columns[0].language_user_overridden);
    REQUIRE(loaded.sheet_mappings[0].columns[0].language_selection_mode == LanguageSelectionMode::Auto);

    WorkbookService service(std::make_unique<OpenXlsxWorkbookReader>());
    auto analysis = service.AnalyzeSheet(FixturePath(), "Vehicle", 2, loaded);
    REQUIRE(FindColumn(analysis.columns, "ARG").language_code == "ar-SA");

    auto config = loaded;
    config.sheet_mappings[0].workbook_identity = analysis.identity;
    config.sheet_mappings[0].columns[0].language_user_overridden = true;
    config.sheet_mappings[0].columns[0].language_selection_mode = LanguageSelectionMode::Fixed;
    WorkbookService::UpsertMapping(config, config.sheet_mappings[0]);
    analysis = service.AnalyzeSheet(FixturePath(), "Vehicle", 2, config);
    REQUIRE(FindColumn(analysis.columns, "ARG").language_code == "en-US");

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void TestJsonConfigV3FixedLanguageSurvivesWhenEqualToGuess() {
    AppConfig config;
    ColumnAnalyzer analyzer;
    auto columns = analyzer.Analyze({"ENG"}, {{"Turn on feature A"}});
    columns[0].selected = true;
    columns[0].role = ColumnRole::Play;
    columns[0].language_code = columns[0].guessed_language;
    columns[0].language_user_overridden = true;
    columns[0].language_selection_mode = LanguageSelectionMode::Fixed;
    config.sheet_mappings.push_back({"fixture:vehicle", "Vehicle", 2, columns});

    const auto path = test::IsolatedRoot() / "adayo_config_store_test" / "config_v3_fixed_equal_guess.json";
    JsonConfigStore store(path);
    store.Save(config);
    const auto loaded = store.Load();
    REQUIRE(loaded.schema_version == 4);
    REQUIRE(loaded.sheet_mappings[0].columns[0].language_code == columns[0].guessed_language);
    REQUIRE(loaded.sheet_mappings[0].columns[0].language_user_overridden);
    REQUIRE(loaded.sheet_mappings[0].columns[0].language_selection_mode == LanguageSelectionMode::Fixed);

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
    const auto path = test::IsolatedRoot() / "adayo_identity_version_test.xlsx";
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
    const auto root = test::IsolatedRoot() / "adayo_model_registry_test";
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

void WriteMinimalModel(const std::filesystem::path& dir, const std::string& id) {
    std::filesystem::create_directories(dir / "espeak-ng-data");
    std::ofstream(dir / "model.onnx").put('\0');
    std::ofstream(dir / "tokens.txt").put('\0');
    std::ofstream config_file(dir / "model.json", std::ios::binary | std::ios::trunc);
    config_file << R"({"id":")" << id
                << R"(","display_name":")" << id
                << R"(","engine_id":"sherpa-vits","model":"model.onnx","tokens":"tokens.txt","data_dir":"espeak-ng-data","language_code":"en-US"})";
}

void TestModelRegistryRejectsDuplicateIdsAndEscapingPaths() {
    const auto root = test::IsolatedRoot() / "adayo_model_registry_security_test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    WriteMinimalModel(root / "dup-a", "dup");
    WriteMinimalModel(root / "dup-b", "dup");
    WriteMinimalModel(root / "good", "good");
    std::filesystem::create_directories(root / "escape");
    {
        std::ofstream(root / "outside.onnx").put('\0');
        std::ofstream(root / "escape" / "tokens.txt").put('\0');
        std::ofstream config_file(root / "escape" / "model.json", std::ios::binary | std::ios::trunc);
        config_file << R"({"id":"escape","engine_id":"sherpa-vits","model":"../outside.onnx","tokens":"tokens.txt","language_code":"en-US"})";
    }
    std::filesystem::create_directories(root / "wrong-engine");
    {
        std::ofstream(root / "wrong-engine" / "model.onnx").put('\0');
        std::ofstream(root / "wrong-engine" / "tokens.txt").put('\0');
        std::ofstream config_file(root / "wrong-engine" / "model.json", std::ios::binary | std::ios::trunc);
        config_file << R"({"id":"wrong-engine","engine_id":"other","model":"model.onnx","tokens":"tokens.txt","language_code":"en-US"})";
    }

    ModelRegistry registry(root);
    const auto scan = registry.ScanSherpaModelsWithDiagnostics();
    REQUIRE(scan.entries.size() == 1);
    REQUIRE(scan.entries[0].id == "good");
    bool duplicate = false;
    bool escape = false;
    bool engine = false;
    for (const auto& invalid : scan.invalid) {
        duplicate = duplicate || invalid.error.find("重复 model id") != std::string::npos;
        escape = escape || invalid.error.find("越出 voice 根目录") != std::string::npos;
        engine = engine || invalid.error.find("ENGINE_MISMATCH") != std::string::npos;
    }
    REQUIRE(duplicate);
    REQUIRE(escape);
    REQUIRE(engine);
    std::filesystem::remove_all(root, ec);
}

void TestModelRegistryExpandsCompleteSpeakers() {
    const auto root = test::IsolatedRoot() / "speaker_registry";
    WriteMinimalModel(root / "multi", "multi");
    const auto path = root / "multi" / "model.json";
    nlohmann::json config;
    { std::ifstream input(path); input >> config; }
    config["speaker_id"] = 1;
    config["num_speakers"] = 3;
    config["speakers"] = {{{"id", 0}, {"name", "Zero"}}, {{"id", 1}, {"name", "One"}}, {{"id", 2}, {"name", "Two"}}};
    const auto save = [&] { std::ofstream(path) << config.dump(); };
    save();
    const auto scan = ModelRegistry(root).ScanSherpaModelsWithDiagnostics();
    REQUIRE(scan.invalid.empty()); REQUIRE(scan.entries.size() == 3);
    REQUIRE(scan.entries[0].id == "multi"); REQUIRE(scan.entries[0].config.speaker_id == 1);
    REQUIRE(scan.entries[1].id == "multi::speaker-0"); REQUIRE(scan.entries[1].config.speaker_id == 0);
    REQUIRE(scan.entries[2].config.speaker_id == 2);
    REQUIRE(scan.entries[0].config.model_path == scan.entries[2].config.model_path);
    config["speakers"][2]["id"] = 0; save();
    REQUIRE(ModelRegistry(root).ScanSherpaModels().empty());
    config["speakers"][2]["id"] = 2; config["speaker_id"] = 3; save();
    REQUIRE(ModelRegistry(root).ScanSherpaModels().empty());
    config["speaker_id"] = 1; config["speakers"].erase(2); save();
    REQUIRE(ModelRegistry(root).ScanSherpaModels().empty());
    config["speakers"].push_back({{"id", 2.5}, {"name", "Fraction"}}); save();
    REQUIRE(ModelRegistry(root).ScanSherpaModels().empty());
    config.erase("speakers"); save();
    REQUIRE(ModelRegistry(root).ScanSherpaModels().empty());
    config.erase("num_speakers"); config["model"]="espeak-ng-data"; save();
    REQUIRE(ModelRegistry(root).ScanSherpaModels().empty());
    config["model"]="model.onnx"; config["data_dir"]="tokens.txt"; save();
    REQUIRE(ModelRegistry(root).ScanSherpaModels().empty());
}

void TestVoiceObservationDoesNotGrantAdmission() {
    const auto app=test::IsolatedRoot()/"observation-app";
    const auto root=app/"model"/"sherpa";
    WriteMinimalModel(root/"multi","multi");
    REQUIRE(!ModelRegistry(root).ScanSherpaModels().at(0).observation);
    nlohmann::json observation={{"model_id","multi"},{"observed_at","2026-09-06T00:00:00Z"},
        {"run_id","isolated-test"},{"model_sha256",std::string(64,'a')},{"frontend_rule_coverage","weight only"},
        {"evidence_scope","finite PCM, not listening acceptance"},{"warnings",nlohmann::json::array()}};
    auto document=nlohmann::json{{"schema_version",1},{"voices",nlohmann::json::array({observation})}};
    const auto save=[&] { const auto bytes=document.dump(); WriteBinaryFile(app/"voice-validation-observations.json",bytes.data(),bytes.size()); };
    save();
    auto scan=ModelRegistry(root).ScanSherpaModelsWithDiagnostics();
    REQUIRE(scan.invalid.empty()); REQUIRE(scan.entries.size()==1); REQUIRE(scan.entries[0].observation);
    REQUIRE(scan.entries[0].observation->Status().find("当前身份未核对")!=std::string::npos);
    REQUIRE(scan.entries[0].observation->Status(std::string(64,'a')).find("非完整当前前端证明")!=std::string::npos);
    REQUIRE(scan.entries[0].observation->Status(std::string(64,'b')).find("已过期")!=std::string::npos);
    document["voices"][0]["warnings"]={"Skip unknown phoneme: fixture"}; save();
    REQUIRE(ModelRegistry(root).ScanSherpaModels()[0].observation->Status().find("曾出现音素告警")!=std::string::npos);
    document["schema_version"]=99; save();
    scan=ModelRegistry(root).ScanSherpaModelsWithDiagnostics();
    REQUIRE(scan.entries.size()==1); REQUIRE(!scan.entries[0].observation); REQUIRE(!scan.entries[0].observation_error.empty());
    WriteBinaryFile(root/"multi"/".preparation-incomplete.json","{}",2);
    scan=ModelRegistry(root).ScanSherpaModelsWithDiagnostics();
    REQUIRE(scan.entries.empty()); REQUIRE(scan.invalid.size()==1);
    REQUIRE(scan.invalid[0].error.find("MODEL_UPDATE_INCOMPLETE")!=std::string::npos);
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

void TestExternalReviewWorkbook() {
    const auto* environment=std::getenv("ADAYO_REVIEW_WORKPACK");
    if(!environment) { std::cout << "External private workbook: NOT_RUN (ADAYO_REVIEW_WORKPACK not set)\n"; return; }
    const auto root=PathFromUtf8(environment);
    std::ifstream input(root/"fixtures"/"workbook_acceptance.json"); const auto fixture=nlohmann::json::parse(input);
    const auto path=root/PathFromUtf8(fixture.at("source_file").get<std::string>());
    const auto hash=FileSha256(path); REQUIRE(hash==fixture.at("sha256").get<std::string>());
    OpenXlsxWorkbookReader reader; const auto sheets=reader.SheetMetadata(path);
    REQUIRE(sheets.size()==13); REQUIRE(reader.SheetNames(path).size()==13);
    REQUIRE(std::count_if(sheets.begin(),sheets.end(),[](const auto& s){return s.visibility==SheetVisibility::Visible;})==12);
    std::map<std::string,WorksheetData> loaded;
    std::size_t merges=0;
    for(const auto& expected:fixture.at("sheets")) {
        const auto name=expected.at("name").get<std::string>();
        auto data=reader.ReadSheet(path,name,1);
        REQUIRE(data.merged_ranges.size()==expected.at("merge_count").get<std::size_t>());
        REQUIRE(data.headers.size()==expected.at("value_last_col").get<std::size_t>());
        REQUIRE(data.source_excel_row_numbers.size()==data.rows.size());
        merges+=data.merged_ranges.size(); loaded[name]=std::move(data);
    }
    REQUIRE(merges==1118);
    for(const auto& expected:fixture.at("cells")) {
        const auto& data=loaded.at(expected.at("sheet").get<std::string>());
        const auto coordinate=expected.at("coordinate").get<std::string>(); std::size_t split=0,column=0;
        while(split<coordinate.size() && coordinate[split]>='A' && coordinate[split]<='Z') column=column*26+coordinate[split++]-'A'+1;
        const auto row=std::stoull(coordinate.substr(split)); std::string actual;
        if(row==1 && column<=data.headers.size()) actual=data.headers[column-1];
        else {
            const auto it=std::find(data.source_excel_row_numbers.begin(),data.source_excel_row_numbers.end(),row);
            if(it!=data.source_excel_row_numbers.end() && column<=data.headers.size()) actual=data.rows[it-data.source_excel_row_numbers.begin()][column-1];
        }
        const auto& value=expected.at("raw_value");
        const auto wanted=value.is_null()?std::string{}:(value.is_string()?value.get<std::string>():value.dump());
        if(actual!=wanted) std::cerr << "Coordinate mismatch: " << expected.at("sheet").get<std::string>() << '!' << coordinate << '\n';
        REQUIRE(actual==wanted);
    }
    CorpusViewService service;
    for(const std::string name:{"通讯","媒体","车控","设置","二次交互","帮助"}) {
        auto columns=ColumnAnalyzer{}.Analyze(loaded.at(name).headers,loaded.at(name).rows);
        for(const auto& column:columns) {
            if(column.header.empty()) REQUIRE(column.role==ColumnRole::Ignore);
            if(column.header=="说法举例") REQUIRE(column.guessed_language.empty());
        }
        auto session=service.CreateSessionFromWorksheet(loaded.at(name),{
            {5,"参考","F",ColumnRole::Reference,"","",""}, {6,"英语","G",ColumnRole::Play,"en-US","sherpa-vits","voice"}});
        REQUIRE(!session.view.rows.empty());
        if(name=="通讯") {
            const auto original=session.source_rows[0][5];
            for(std::size_t i=0;i<4;++i) { REQUIRE(session.view.rows[i][1]==original); REQUIRE(session.view.row_meta[i].reference_owner_excel_row==2); service.CycleResult(session,i,3); }
            service.CycleResult(session,4,3);
            const auto impact=service.UpdateDisplayCell(session,2,1,"edited reference");
            REQUIRE(impact.raw_rows.size()==4);
            for(std::size_t i=0;i<4;++i) { REQUIRE(session.view.rows[i][1]=="edited reference"); REQUIRE(session.view.rows[i][3].empty()); }
            REQUIRE(!session.view.rows[4][3].empty()); REQUIRE(session.source_rows[2][5].empty());
            for(std::size_t i=4;i<7;++i) REQUIRE(session.view.rows[i][1]==loaded.at(name).rows[4][5]);
        }
    }
    const auto& history=loaded.at("变更记录");
    REQUIRE(std::find(history.source_excel_row_numbers.begin(),history.source_excel_row_numbers.end(),10)!=history.source_excel_row_numbers.end());
    REQUIRE(FileSha256(path)==hash);
    std::cout << "PASS external original workbook: 13 sheets, 12 visible, 1118 merges, " << fixture.at("cells").size() << " coordinates, six sessions, merged reference edits\n";
}

void TestEditImpactWhenShapeUnchanged() {
    CorpusViewService service;
    auto session=service.CreateSession({{"reference","a1","b1\nb2\nb3"}}, {
        {0,"Reference","A",ColumnRole::Reference},{1,"English","B",ColumnRole::Play},{2,"Other","C",ColumnRole::Play}});
    service.CycleResult(session,1,5);
    const auto impact=service.UpdateDisplayCell(session,0,2,"a1\na2");
    REQUIRE(session.view.rows.size()==3); REQUIRE(session.view.rows[1][2]=="a2");
    REQUIRE(impact.segment_structure_changed); REQUIRE(impact.display_rows.size()==3); REQUIRE(!session.view.rows[1][5].empty());
}

#ifdef ADAYO_CAN_TEST_RUNTIME
static void TestRuntimeSaveOrderingAndControlledShutdown() {
    const auto root=test::IsolatedRoot()/"runtime-lifecycle";
    std::filesystem::create_directory(root);
    struct Io final:AtomicFileOperations {
        std::promise<void> entered,release;
        std::shared_future<void> released=release.get_future().share();
        std::atomic<bool> block{},fail{};
        std::size_t Write(std::FILE* file,const void* data,std::size_t size) override {
            if(block.exchange(false)) { entered.set_value(); released.wait(); }
            return AtomicFileOperations::Write(file,data,size);
        }
        void Replace(const std::filesystem::path& from,const std::filesystem::path& to) override {
            if(fail.exchange(false)) throw std::runtime_error("deterministic config replace failure");
            AtomicFileOperations::Replace(from,to);
        }
    } io;
    {
    ApplicationRuntime runtime(root,&io);
    std::promise<void> old_requested,allow_old;
    auto allowed=allow_old.get_future().share();
    auto old=std::async(std::launch::async,[&] { old_requested.set_value(); allowed.wait(); runtime.SaveConfig(); });
    old_requested.get_future().get();
    runtime.UpdateConfig([](AppConfig& config) { config.audio_cache.disk_limit_bytes=1024ull*1024*1024; });
    runtime.UpdateConfig([](AppConfig& config) { config.compare=CompareService::Preset("asr_cer_v1"); });
    runtime.SaveConfig();
    allow_old.set_value(); old.get();
    JsonConfigStore store(root/"config/config.json");
    REQUIRE(store.Load().audio_cache.disk_limit_bytes==1024ull*1024*1024);
    REQUIRE(store.Load().compare.profile_id=="asr_cer_v1");

    io.block=true;
    auto saving=std::async(std::launch::async,[&] { runtime.SaveConfig(); });
    io.entered.get_future().get();
    auto editing=std::async(std::launch::async,[&] {
        runtime.UpdateConfig([](AppConfig& config) { config.last_sheet="isolated"; });
    });
    const bool update_during_io=editing.wait_for(std::chrono::seconds(2))==std::future_status::ready;
    io.release.set_value(); saving.get(); editing.get();
    REQUIRE(update_during_io);
    runtime.SaveConfig();
    const auto saved_hash=FileSha256(store.Path());
    io.fail=true;
    runtime.UpdateConfig([](AppConfig& config) { config.audio_cache.enabled=false; });
    bool failed=false;
    try { runtime.SaveConfig(); } catch(const std::exception&) { failed=true; }
    REQUIRE(failed); REQUIRE(FileSha256(store.Path())==saved_hash);
    REQUIRE(runtime.ConfigSnapshot().compare.profile_id=="asr_cer_v1");
    runtime.SaveConfig();
    REQUIRE(!store.Load().audio_cache.enabled); REQUIRE(store.Load().last_sheet=="isolated");

    io.entered=std::promise<void>{}; io.release=std::promise<void>{};
    io.released=io.release.get_future().share(); io.block=true; io.fail=true;
    auto failed_update=std::async(std::launch::async,[&] {
        try {
            runtime.SaveConfig([](AppConfig& config) { config.compare=CompareService::Preset("strict_rows_v1"); },
                [](AppConfig& config,const AppConfig& before) { config.compare=before.compare; });
            return false;
        } catch(const std::runtime_error&) { return true; }
    });
    io.entered.get_future().get();
    runtime.UpdateConfig([](AppConfig& config) { config.last_sheet="concurrent-field"; });
    std::promise<void> next_requested;
    auto next=std::async(std::launch::async,[&] {
        next_requested.set_value();
        runtime.SaveConfig([](AppConfig& config) { config.audio_cache.enabled=true; },
            [](AppConfig& config,const AppConfig& before) { config.audio_cache=before.audio_cache; });
    });
    next_requested.get_future().get(); io.release.set_value();
    REQUIRE(failed_update.get()); next.get();
    REQUIRE(runtime.ConfigSnapshot().compare.profile_id=="asr_cer_v1");
    REQUIRE(store.Load().compare.profile_id=="asr_cer_v1");
    REQUIRE(store.Load().last_sheet=="concurrent-field"); REQUIRE(store.Load().audio_cache.enabled);

#ifdef _WIN32
    const auto before_locked_save=FileSha256(store.Path());
    const auto handle=CreateFileW(store.Path().c_str(),GENERIC_READ,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    REQUIRE(handle!=INVALID_HANDLE_VALUE);
    const auto close=[](void* value) { CloseHandle(value); };
    std::unique_ptr<void,decltype(close)> held(handle,close);
    std::string locked_save_error;
    try {
        runtime.SaveConfig([](AppConfig& config) { config.audio_cache.enabled=false; },
            [](AppConfig& config,const AppConfig& before) { config.audio_cache=before.audio_cache; });
    } catch(const std::exception& ex) { locked_save_error=ex.what(); }
    REQUIRE(!locked_save_error.empty());
    REQUIRE(FileSha256(store.Path())==before_locked_save);
    REQUIRE(runtime.ConfigSnapshot().audio_cache.enabled);
    REQUIRE(store.Load().audio_cache.enabled);
    held.reset();
    runtime.SaveConfig([](AppConfig& config) { config.audio_cache.enabled=false; },
        [](AppConfig& config,const AppConfig& before) { config.audio_cache=before.audio_cache; });
    REQUIRE(!store.Load().audio_cache.enabled);
    std::cout << "PASS runtime real config sharing violation: unchanged bytes, field rollback, unlocked retry\n";
#endif

    std::promise<void> job_entered,job_release;
    auto job_released=job_release.get_future().share();
    runtime.BackgroundJobs().Submit([&](std::stop_token) { job_entered.set_value(); job_released.wait(); });
    job_entered.get_future().get();
    runtime.RequestShutdown(); runtime.RequestShutdown();
    const bool was_waiting=!runtime.ShutdownComplete();
    bool job_rejected=false;
    try { runtime.BackgroundJobs().Submit([](std::stop_token) {}); } catch(const std::exception&) { job_rejected=true; }
    job_release.set_value(); runtime.Shutdown();
    REQUIRE(was_waiting); REQUIRE(job_rejected); REQUIRE(runtime.ShutdownComplete());
    runtime.Shutdown();
    }
    for(int restart=0;restart<3;++restart) {
        ApplicationRuntime restarted(root);
        const auto restored=restarted.ConfigSnapshot();
        REQUIRE(restored.compare.profile_id=="asr_cer_v1");
        REQUIRE(restored.audio_cache.disk_limit_bytes==1024ull*1024*1024);
        REQUIRE(restored.last_sheet=="concurrent-field");
#ifdef _WIN32
        REQUIRE(!restored.audio_cache.enabled);
#endif
        restarted.Shutdown();
        REQUIRE(restarted.ShutdownComplete());
    }
    std::cout << "PASS runtime save ordering: delayed old save, IO barrier, field rollback, three complete runtime restarts\n";
}
#endif

int main() {
    return test::RunTestMain("adayo_p2_tests", [] {
        TestWorkbookReadAndAnalyze();
        TestWorkbookReadFromChinesePath();
        TestRuntimeViewFromFixture();
        TestJsonConfigStoreRoundTrip();
#ifdef ADAYO_CAN_TEST_RUNTIME
        TestRuntimeSaveOrderingAndControlledShutdown();
#endif
        TestJsonConfigCorruptBackupAndSafeSave();
        TestJsonConfigFutureSchemaRefusesOverwrite();
        TestJsonConfigV1LanguageMigrationDoesNotOverrideAnalyzer();
        TestJsonConfigV3FixedLanguageSurvivesWhenEqualToGuess();
        {
            const auto root=test::IsolatedRoot()/("adayo-schema4-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directory(root);
            const std::string old=R"({"schema_version":3,"speech_rate":1.4,"alignment_threshold":83,"pass_threshold":99})";
            WriteBinaryFile(root/"config.json",old.data(),old.size());
            JsonConfigStore store(root/"config.json"); auto migrated=store.Load();
            REQUIRE(migrated.schema_version==4); REQUIRE(migrated.speech_rate==1.4); REQUIRE(migrated.alignment_threshold==83); REQUIRE(migrated.pass_threshold==99);
            REQUIRE(migrated.compare.profile_id=="legacy_v1");
            REQUIRE(migrated.compare.alignment.alignment_threshold==83); REQUIRE(migrated.compare.pass_threshold==99);
            for(const auto* profile:{"legacy_v1","strict_rows_v1","asr_cer_v1","asr_wer_v1"}) {
                migrated.compare=CompareService::Preset(profile);
                migrated.compare.custom=true;
                migrated.compare.delimiter="||";
                migrated.compare.alignment.anchor_uniqueness_margin=8;
                migrated.compare.max_error_rate=2.5;
                store.Save(migrated);
                REQUIRE(store.Load().compare==migrated.compare);
                REQUIRE(store.Load().alignment_threshold==83);
            }
            REQUIRE(migrated.audio_cache.enabled); REQUIRE(migrated.audio_cache.disk_limit_bytes==2147483648ull);
            store.Save(migrated); REQUIRE(store.Load().speech_rate==1.4);
            const std::string bad=R"({"schema_version":4,"audio_cache":{"disk_limit_bytes":0}})";
            WriteBinaryFile(root/"config.json",bad.data(),bad.size());
            const auto before=FileSha256(root/"config.json"); const auto rejected=store.LoadOrDefault();
            REQUIRE(rejected.status==ConfigLoadStatus::InvalidValues); REQUIRE(!rejected.allow_save);
            REQUIRE(FileSha256(root/"config.json")==before);
        }
        TestWorkbookMappingIdentityIncludesHeaderRow();
        TestWorkbookIdentityIgnoresContentVersion();
        TestSavedLanguageOverrideSemantics();
        TestModelRegistryKeepsValidModelsWhenOneIsBroken();
        TestModelRegistryRejectsDuplicateIdsAndEscapingPaths();
        TestModelRegistryExpandsCompleteSpeakers();
        TestVoiceObservationDoesNotGrantAdmission();
        TestCorpusViewEditAndResultCycle();
        TestResultIdentitySurvivesEarlierRowStructureEdit();
        TestSyntheticBlankCannotBeEditedOrMarked();
        TestEditImpactWhenShapeUnchanged();
        TestExternalReviewWorkbook();
    });
}
