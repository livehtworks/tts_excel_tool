#include "adapters/excel/LibXlsxWriterExporter.h"
#include "core/workbook/ViewBuilder.h"
#include "services/CompareService.h"

#ifdef ADAYO_CAN_VERIFY_XLSX_READ
#include "adapters/excel/OpenXlsxWorkbookReader.h"
#endif

#include "TestCheck.h"

#include <filesystem>
#include <iostream>

using namespace adayo;

namespace {
std::filesystem::path OutputDir() {
    auto dir = std::filesystem::temp_directory_path() / "adayo_中文导出验证";
    std::filesystem::create_directories(dir);
    return dir;
}

RuntimeView MakeRuntimeView() {
    ViewBuilder builder;
    std::vector<std::vector<std::string>> rows = {
        {"开关控制", "Turn on feature A\nEnable feature A", "你好"},
        {"阿语验证", "مرحبا", "再见"},
    };
    std::vector<SelectedColumn> columns = {
        {0, "三级功能", "A", ColumnRole::Reference, "", ""},
        {1, "ENG", "B", ColumnRole::Play, "en-US", "sherpa-vits"},
        {2, "中文", "C", ColumnRole::Play, "zh-CN", "sherpa-vits"},
    };
    std::unordered_map<ResultIdentity, std::string, ResultIdentityHash> results = {
        {{0, 1, 0}, "pass"},
        {{0, 1, 1}, "fail"},
    };
    return builder.Build(rows, columns, results).view;
}

std::vector<CompareRow> MakeCompareRows() {
    CompareService service;
    CompareOptions options;
    options.pass_threshold = 100.0;
    return service.Compare(
        {"温度设置为22度", "删除字符ABC", "مرحبا بالعالم"},
        {"温度设置为23度", "删除字符AC", "مرحبا بالعالم"},
        options);
}

void TestRuntimeExport() {
    std::cout << "P7 runtime export\n" << std::flush;
    const auto output = OutputDir() / "运行视图.xlsx";
    LibXlsxWriterExporter exporter;
    exporter.ExportRuntimeView(MakeRuntimeView(), output);
    REQUIRE(std::filesystem::exists(output));
    REQUIRE(std::filesystem::file_size(output) > 0);

#ifdef ADAYO_CAN_VERIFY_XLSX_READ
    std::cout << "P7 runtime readback\n" << std::flush;
    OpenXlsxWorkbookReader reader;
    const auto data = reader.ReadSheet(output, "运行视图", 1);
    REQUIRE(data.headers.size() == 6);
    REQUIRE(data.rows[0][1] == "开关控制");
    REQUIRE(data.rows[0][2] == "Turn on feature A");
    REQUIRE(data.rows[0][3] == "✔");
#endif
}

void TestComparisonExport() {
    std::cout << "P7 comparison export\n" << std::flush;
    const auto output = OutputDir() / "对比报告.xlsx";
    LibXlsxWriterExporter exporter;
    exporter.ExportComparison(MakeCompareRows(), output);
    REQUIRE(std::filesystem::exists(output));
    REQUIRE(std::filesystem::file_size(output) > 0);

#ifdef ADAYO_CAN_VERIFY_XLSX_READ
    std::cout << "P7 comparison readback\n" << std::flush;
    OpenXlsxWorkbookReader reader;
    const auto data = reader.ReadSheet(output, "文本对比", 1);
    REQUIRE(data.headers.size() == 4);
    REQUIRE(data.rows[0][0] == "温度设置为22度");
    REQUIRE(data.rows[0][1] == "温度设置为23度");
    REQUIRE(data.rows[0][3] == "NG");
    REQUIRE(data.rows[2][0] == "مرحبا بالعالم");
#endif
}

void TestComparisonGroupExport() {
    std::cout << "P7 comparison group export\n" << std::flush;
    const auto output = OutputDir() / "多语言对比报告.xlsx";
    CompareService service;
    CompareOptions options;
    options.pass_threshold = 100.0;
    std::vector<CompareReportGroup> groups = {
        {"中文", service.Compare({"打开空调"}, {"打开空调"}, options)},
        {"English", service.Compare({"Turn on lights"}, {"Turn off lights"}, options)},
    };

    LibXlsxWriterExporter exporter;
    exporter.ExportComparisonGroups(groups, output);
    REQUIRE(std::filesystem::exists(output));
    REQUIRE(std::filesystem::file_size(output) > 0);

#ifdef ADAYO_CAN_VERIFY_XLSX_READ
    std::cout << "P7 comparison group readback\n" << std::flush;
    OpenXlsxWorkbookReader reader;
    const auto data = reader.ReadSheet(output, "文本对比", 1);
    REQUIRE(data.headers.size() == 8);
    REQUIRE(data.headers[0] == "中文 正式文本");
    REQUIRE(data.headers[4] == "English 正式文本");
    REQUIRE(data.rows[0][0] == "打开空调");
    REQUIRE(data.rows[0][3] == "OK");
    REQUIRE(data.rows[0][4] == "Turn on lights");
    REQUIRE(data.rows[0][7] == "NG");
#endif
}
} // namespace

int main() {
    return test::RunTestMain("adayo_p7_export_tests", [] {
        TestRuntimeExport();
        TestComparisonExport();
        TestComparisonGroupExport();
    });
}
