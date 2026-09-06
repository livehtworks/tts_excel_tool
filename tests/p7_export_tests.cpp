#include "adapters/excel/LibXlsxWriterExporter.h"
#include "core/workbook/ViewBuilder.h"
#include "services/CompareService.h"
#include "platform/FileIo.h"
#include "platform/UnicodePath.h"
#include "services/CorpusViewService.h"
#include <fstream>
#include <cstdlib>
#include <nlohmann/json.hpp>

#ifdef ADAYO_CAN_VERIFY_XLSX_READ
#include "adapters/excel/OpenXlsxWorkbookReader.h"
#endif

#include "TestCheck.h"

#include <filesystem>
#include <iostream>

using namespace adayo;

namespace {
std::filesystem::path OutputDir() {
    auto dir = test::IsolatedRoot() / PathFromUtf8("adayo_中文导出验证");
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
    auto view=builder.Build(rows, columns, results).view;
    view.source={"synthetic-source.xlsx","source-identity","synthetic-hash","original-sheet","2026-09-06",1};
    for(auto& meta:view.row_meta) { meta.source_excel_row=meta.raw_row_index+2; meta.reference_owner_excel_row=meta.source_excel_row; }
    return view;
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
    const auto output = OutputDir() / PathFromUtf8("运行视图.xlsx");
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
    const auto source=reader.ReadSheet(output,"来源",1);
    REQUIRE(source.rows[0][1]=="synthetic-source.xlsx");
    const auto mapping=reader.ReadSheet(output,"坐标映射",1);
    REQUIRE(mapping.rows[1][6]=="A2"); REQUIRE(mapping.rows[1][7]=="A2");
#endif
}

void TestComparisonExport() {
    std::cout << "P7 comparison export\n" << std::flush;
    const auto output = OutputDir() / PathFromUtf8("对比报告.xlsx");
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
    const auto output = OutputDir() / PathFromUtf8("多语言对比报告.xlsx");
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

void TestMetricAndLimitExports() {
    LibXlsxWriterExporter exporter;
    std::vector<CompareReportGroup> groups;
    for(const auto* profile:{"strict_rows_v1","asr_cer_v1","asr_wer_v1"}) {
        CompareReportGroup group;
        group.label=profile; group.options=CompareService::Preset(profile);
        group.reference_source={"reference.txt",Sha256("a")}; group.actual_source={"actual.txt",Sha256("b")};
        group.rows=CompareService{}.Compare({"a","","e\xCC\x81"},{"aaaa","abc","é"},group.options);
        groups.push_back(std::move(group));
    }
    const auto output=OutputDir()/"metric-report.xlsx";
    exporter.ExportComparisonGroups(groups,output);
#ifdef ADAYO_CAN_VERIFY_XLSX_READ
    OpenXlsxWorkbookReader reader;
    const auto data=reader.ReadSheet(output,"文本对比",1);
    REQUIRE(data.headers[2]=="strict_rows_v1 Levenshtein相似度(%)");
    REQUIRE(data.headers[6]=="asr_cer_v1 字错误率(%)");
    REQUIRE(data.rows[0][6]=="300");
    REQUIRE(data.rows[1][6].find("未定义")!=std::string::npos);
    REQUIRE(reader.ReadSheet(output,"参数",1).rows.size()>=80);
    const auto statistics=reader.ReadSheet(output,"统计与来源",1);
    REQUIRE(statistics.rows[0][2]=="1"); REQUIRE(statistics.rows[0][5]=="4");
#endif
    const auto hash=FileSha256(output);
    auto reject=[&](auto action) {
        bool failed=false; try{action();}catch(const std::exception&){failed=true;}
        REQUIRE(failed); REQUIRE(FileSha256(output)==hash);
    };
    reject([&]{exporter.ExportComparisonGroups(std::vector<CompareReportGroup>(4097),output);});
    auto view=MakeRuntimeView();
    view.source.path=PathToUtf8(output);
    reject([&]{exporter.ExportRuntimeView(view,output);});
    const auto alias=OutputDir()/"source-hardlink.xlsx";
    std::filesystem::create_hard_link(output,alias);
    reject([&]{exporter.ExportRuntimeView(view,alias);});
    REQUIRE(std::filesystem::equivalent(output,alias));
    view=MakeRuntimeView();
    view.rows[0][0]=std::string(32768,'x');
    reject([&]{exporter.ExportRuntimeView(view,output);});
    view=MakeRuntimeView(); view.rows[0][0]=std::string("a\0b",3);
    reject([&]{exporter.ExportRuntimeView(view,output);});
    view=MakeRuntimeView(); view.headers.resize(16385);
    reject([&]{exporter.ExportRuntimeView(view,output);});
    groups[0].rows[0].diff.reference_fragments.clear();
    reject([&]{exporter.ExportComparisonGroups(groups,output);});
}

void TestExternalWorkbookExport() {
#ifdef ADAYO_CAN_VERIFY_XLSX_READ
    if(const auto* environment=std::getenv("ADAYO_REVIEW_WORKPACK")) {
        const auto root=PathFromUtf8(environment);
        std::ifstream input(root/"fixtures"/"workbook_acceptance.json");
        const auto fixture=nlohmann::json::parse(input);
        const auto source=root/PathFromUtf8(fixture.at("source_file").get<std::string>());
        const auto hash=FileSha256(source);
        OpenXlsxWorkbookReader reader;
        std::size_t exported=0;
        for(const auto& info:reader.SheetMetadata(source)) {
            auto data=reader.ReadSheet(source,info.name,1);
            if(data.headers.size()<7) continue;
            std::vector<SelectedColumn> columns={{5,data.headers[5],"F",ColumnRole::Reference},{6,data.headers[6],"G",ColumnRole::Play,"en-US","sherpa-vits"}};
            auto session=CorpusViewService{}.CreateSessionFromWorksheet(std::move(data),columns);
            if(session.view.rows.empty()) continue;
            const auto output=OutputDir()/("original-workbook-"+std::to_string(exported++)+".xlsx");
            LibXlsxWriterExporter{}.ExportRuntimeView(session.view,output);
            const auto report=reader.ReadSheet(output,"运行视图",1);
            REQUIRE(report.rows==session.view.rows);
            const auto meta=reader.ReadSheet(output,"来源",1);
            REQUIRE(meta.rows[2][1]==hash);
            const auto mapped=reader.ReadSheet(output,"坐标映射",1);
            REQUIRE(mapped.rows.size()==session.view.rows.size()*session.view.columns.size());
            REQUIRE(mapped.rows[1][6]=="F"+std::to_string(session.view.row_meta[0].source_excel_row));
        }
        REQUIRE(exported>=6); REQUIRE(FileSha256(source)==hash);
        std::cout<<"PASS original workbook export/readback count="<<exported<<"\n";
    } else std::cout<<"External workbook export NOT_RUN\n";
#endif
}

int main() {
    return test::RunTestMain("adayo_p7_export_tests", [] {
        TestRuntimeExport();
        TestComparisonExport();
        TestComparisonGroupExport();
        TestMetricAndLimitExports();
        TestExternalWorkbookExport();
    });
}
