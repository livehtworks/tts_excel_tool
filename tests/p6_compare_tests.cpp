#include "adapters/text/TextFileImporter.h"
#include "services/CompareService.h"

#include "TestCheck.h"

#include <chrono>
#include <iostream>
#include <set>
#include <stdexcept>
#include "core/unicode/Utf8.h"
#include "platform/UnicodePath.h"
#include <fstream>
#include <cstdlib>
#include <cmath>
#ifdef ADAYO_HAS_JSON_CONFIG
#include <nlohmann/json.hpp>
#endif

using namespace adayo;

namespace {
CompareOptions SupportedMathOptions() {
    CompareOptions options{};
#ifndef ADAYO_HAS_UTF8PROC
    options.normalizer.normalization=UnicodeNormalization::None;
    options.normalizer.case_fold=false;
#endif
    return options;
}
void AssertUniqueIndexes(const std::vector<CompareRow>& rows) {
    std::set<std::size_t> refs;
    std::set<std::size_t> acts;
    for (const auto& row : rows) {
        if (row.reference_index) REQUIRE(refs.insert(*row.reference_index).second);
        if (row.actual_index) REQUIRE(acts.insert(*row.actual_index).second);
    }
}

bool ContainsStatusForReference(const std::vector<CompareRow>& rows, const std::string& text, CompareStatus status) {
    for (const auto& row : rows) {
        if (row.reference_text == text && row.status == status) return true;
    }
    return false;
}

bool ContainsExtra(const std::vector<CompareRow>& rows, const std::string& text) {
    for (const auto& row : rows) {
        if (row.actual_text == text && row.status == CompareStatus::Extra) return true;
    }
    return false;
}

void TestFixedAlignmentSet() {
#ifndef ADAYO_HAS_UTF8PROC
    bool unsupported=false;
    try { CompareService{}.Compare({"sample"},{"sample"}); } catch(const std::runtime_error& ex) { unsupported=std::string(ex.what()).find("UNSUPPORTED_UNICODE")!=std::string::npos; }
    REQUIRE(unsupported);
    std::cout << "SKIPPED_UNSUPPORTED: full NFKC/casefold fixture requires utf8proc; explicit rejection verified\n";
    return;
#endif
    CompareService service;
    auto options=SupportedMathOptions();
    options.alignment.alignment_threshold = 75.0;
    options.alignment.anchor_threshold = 95.0;
    options.pass_threshold = 100.0;
    options.normalizer.ignore_punctuation = true;
    options.normalizer.case_fold = true;
    options.normalizer.normalization = UnicodeNormalization::Nfkc;

    const std::vector<std::string> reference = {
        "打开空调",
        "温度设置为22度",
        "Turn ON lights!",
        "مرحبا بالعالم",
        "重复短句",
        "重复短句",
        "缺失一",
        "缺失二",
        "缺失三",
        "ＡＢＣ１２３",
    };
    const std::vector<std::string> actual = {
        "打开空调",
        "温度设置为23度",
        "turn on lights",
        "مرحبا بالعالم",
        "重复短句",
        "机器额外句",
        "重复短句",
        "ABC123",
    };

    const auto rows = service.Compare(reference, actual, options);
    AssertUniqueIndexes(rows);
    REQUIRE(ContainsStatusForReference(rows, "打开空调", CompareStatus::Ok));
    REQUIRE(ContainsStatusForReference(rows, "温度设置为22度", CompareStatus::Ng));
    REQUIRE(ContainsStatusForReference(rows, "Turn ON lights!", CompareStatus::Ok));
    REQUIRE(ContainsStatusForReference(rows, "مرحبا بالعالم", CompareStatus::Ok));
    REQUIRE(ContainsStatusForReference(rows, "缺失一", CompareStatus::Missing));
    REQUIRE(ContainsStatusForReference(rows, "缺失二", CompareStatus::Missing));
    REQUIRE(ContainsStatusForReference(rows, "缺失三", CompareStatus::Missing));
#ifdef ADAYO_HAS_UTF8PROC
    REQUIRE(ContainsStatusForReference(rows, "ＡＢＣ１２３", CompareStatus::Ok));
#else
    std::cout << "P6 NFKC capability not enabled; skipping full-width NFKC assertion\n";
#endif
    REQUIRE(ContainsExtra(rows, "机器额外句"));
}

void TestPunctuationSwitch() {
    CompareService service;
    auto loose=SupportedMathOptions();
    loose.normalizer.ignore_punctuation = true;
    loose.pass_threshold = 100.0;
    auto rows = service.Compare({"hello!"}, {"hello"}, loose);
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].status == CompareStatus::Ok);

    auto strict=SupportedMathOptions();
    strict.normalizer.ignore_punctuation = false;
    strict.pass_threshold = 100.0;
    rows = service.Compare({"hello!"}, {"hello"}, strict);
    REQUIRE(rows[0].status == CompareStatus::Ng);
}

void TestUnicodePunctuationWithUtf8proc() {
#ifdef ADAYO_HAS_UTF8PROC
    CompareService service;
    auto options=SupportedMathOptions();
    options.normalizer.ignore_punctuation = true;
    options.pass_threshold = 100.0;
    auto rows = service.Compare({"مرحبا، بالعالم؛"}, {"مرحبا بالعالم"}, options);
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].status == CompareStatus::Ok);
#else
    std::cout << "P6 utf8proc punctuation category not enabled; skipping Arabic punctuation assertion\n";
#endif
}

void TestAnchorCannotBypassAlignmentThreshold() {
    CompareService service;
    auto options=SupportedMathOptions();
    options.alignment.alignment_threshold = 99.0;
    options.alignment.anchor_threshold = 95.0;
    options.pass_threshold = 100.0;
    auto rows = service.Compare({"abcdefghij"}, {"abcdefghiX"}, options);
    REQUIRE(rows.size() == 2);
    REQUIRE(ContainsStatusForReference(rows, "abcdefghij", CompareStatus::Missing));
    REQUIRE(ContainsExtra(rows, "abcdefghiX"));
}

void TestInvalidThresholdsRejected() {
    CompareService service;
    auto options=SupportedMathOptions();
    options.alignment.alignment_threshold = 101.0;
    bool threw = false;
    try {
        (void)service.Compare({"a"}, {"a"}, options);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    REQUIRE(threw);

    options = SupportedMathOptions();
    options.alignment.alignment_threshold = 90.0;
    options.pass_threshold = 80.0;
    threw = false;
    try {
        (void)service.Compare({"a"}, {"a"}, options);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    REQUIRE(threw);
}

void TestCompareMatrixBudgetRejectsOversizeInput() {
    CompareService service;
    auto options=SupportedMathOptions();
    options.alignment.alignment_threshold = 70.0;
    options.pass_threshold = 100.0;
    std::vector<std::string> reference(9000, "same");
    std::vector<std::string> actual(9000, "same");
    bool threw = false;
    try {
        (void)service.Compare(reference, actual, options);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    REQUIRE(threw);
}

void TestTextImporterBomAndDelimiter() {
    TextImportOptions newline_options;
    auto records = TextFileImporter::SplitUtf8Records("\xEF\xBB\xBF第一行\r\n第二行\n\n第三行", newline_options);
    REQUIRE(records.size() == 3);
    REQUIRE(records[0] == "第一行");
    REQUIRE(records[1] == "第二行");
    REQUIRE(records[2] == "第三行");

    TextImportOptions delimiter_options;
    delimiter_options.delimiter = "\n---\n";
    records = TextFileImporter::SplitUtf8Records("\xEF\xBB\xBF中文第一条\n---\nEnglish second\n---\nمرحبا\r\n", delimiter_options);
    REQUIRE(records.size() == 3);
    REQUIRE(records[0] == "中文第一条");
    REQUIRE(records[1] == "English second");
    REQUIRE(records[2] == "مرحبا");
}

void TestTextImporterUtf16Bom() {
    std::string utf16le;
    for (unsigned char c : {0xFF, 0xFE, 0x2D, 0x4E, 0x87, 0x65, 0x0A, 0x00, 0x41, 0x00}) {
        utf16le.push_back(static_cast<char>(c));
    }
    auto records = TextFileImporter::SplitUtf8Records(utf16le);
    REQUIRE(records.size() == 2);
    REQUIRE(records[0] == "中文");
    REQUIRE(records[1] == "A");

    std::string utf16be;
    for (unsigned char c : {0xFE, 0xFF, 0x4E, 0x2D, 0x65, 0x87, 0x00, 0x0A, 0x00, 0x42}) {
        utf16be.push_back(static_cast<char>(c));
    }
    records = TextFileImporter::SplitUtf8Records(utf16be);
    REQUIRE(records.size() == 2);
    REQUIRE(records[0] == "中文");
    REQUIRE(records[1] == "B");
}

void TestTextImporterGb18030AndInvalidBytes() {
    std::string gb18030;
    for (unsigned char c : {0xD6, 0xD0, 0xCE, 0xC4, 0x0A, 0x41}) {
        gb18030.push_back(static_cast<char>(c));
    }
    auto records = TextFileImporter::SplitUtf8Records(gb18030);
    REQUIRE(records.size() == 2);
    REQUIRE(records[0] == "中文");
    REQUIRE(records[1] == "A");

    TextImportOptions utf8_only;
    utf8_only.encoding = TextEncoding::Utf8;
    bool threw = false;
    try {
        (void)TextFileImporter::SplitUtf8Records(std::string("\xFF\xFF", 2), utf8_only);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    REQUIRE(threw);
}

void TestTextImporterSkipsUnicodeWhitespaceOnlyRecords() {
    TextImportOptions options;
    auto records = TextFileImporter::SplitUtf8Records("   \n\t\r\n　\n  keep  \n", options);
    REQUIRE(records.size() == 1);
    REQUIRE(records[0] == "  keep  ");
}

void TestPerformance1000() {
    CompareService service;
    auto options=SupportedMathOptions();
    options.alignment.alignment_threshold = 70.0;
    options.alignment.anchor_threshold = 95.0;
    std::vector<std::string> reference;
    std::vector<std::string> actual;
    for (int i = 0; i < 1000; ++i) {
        reference.push_back("语料句子 " + std::to_string(i) + " 打开空调");
        if (i % 127 != 0) {
            actual.push_back("语料句子 " + std::to_string(i) + " 打开空调");
        }
    }

    const auto start = std::chrono::steady_clock::now();
    const auto rows = service.Compare(reference, actual, options);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    AssertUniqueIndexes(rows);
    REQUIRE(rows.size() >= reference.size());
    std::cout << "P6 1000x992 compare elapsed_ms=" << elapsed << "\n";
}

void TestPerformance5000WithRapidfuzz() {
#ifdef ADAYO_HAS_RAPIDFUZZ
    CompareService service;
    auto options=SupportedMathOptions();
    options.alignment.alignment_threshold = 70.0;
    options.alignment.anchor_threshold = 95.0;
    std::vector<std::string> reference;
    std::vector<std::string> actual;
    reference.reserve(5000);
    actual.reserve(5000);
    for (int i = 0; i < 5000; ++i) {
        reference.push_back("batch sentence " + std::to_string(i) + " open climate");
        if (i % 521 != 0) {
            actual.push_back("batch sentence " + std::to_string(i) + " open climate");
        }
    }

    const auto start = std::chrono::steady_clock::now();
    const auto rows = service.Compare(reference, actual, options);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    AssertUniqueIndexes(rows);
    REQUIRE(rows.size() >= reference.size());
    std::cout << "P6 5000x4990 compare elapsed_ms=" << elapsed << "\n";
#else
    std::cout << "P6 rapidfuzz not enabled; skipping 5000-row performance assertion\n";
#endif
}
} // namespace

void TestStrictImportAndIndelGoldens() {
    TextSimilarity similarity;
    struct Golden {std::string left,right; double score;};
    for(const auto& golden:std::vector<Golden>{{"abc","ab",80},{"a","b",0},{"","",100},{"","你好",0},{"打开空调","关闭空调",50},{"温度22","温度23",75},{"ab","ba",50},{"🙂甲","🙂乙",50}})
        REQUIRE(std::abs(similarity.Ratio(golden.left,golden.right)-golden.score)<1e-9);
    REQUIRE(unicode::Encode(std::u32string(1,0xD800))=="\xEF\xBF\xBD");
    TextImportOptions preserve; preserve.empty_records=EmptyRecordPolicy::PreserveInternal;
    REQUIRE(TextFileImporter::SplitUtf8Records("a\n\nb\n",preserve)==std::vector<std::string>({"a","","b"}));
    REQUIRE(TextFileImporter::SplitUtf8Records("\n",preserve)==std::vector<std::string>({""}));
    REQUIRE(TextFileImporter::SplitUtf8Records("",preserve).empty());
    bool rejected=false; try{TextFileImporter::SplitUtf8Records("\xEF\xBB\xBF\xFF");}catch(const std::runtime_error&){rejected=true;} REQUIRE(rejected);
#ifdef ADAYO_HAS_JSON_CONFIG
    if(const auto* environment=std::getenv("ADAYO_REVIEW_WORKPACK")) {
        const auto root=PathFromUtf8(environment);
        std::ifstream input(root/"fixtures"/"text_import_cases.json");const auto fixture=nlohmann::json::parse(input);
        for(const auto& item:fixture.at("cases")) {
            TextImportOptions options;options.skip_empty=item.value("skip_empty",true);options.delimiter=item.value("delimiter",std::string{});
            if(!options.skip_empty) options.empty_records=EmptyRecordPolicy::PreserveInternal;
            const auto encoding=item.at("encoding").get<std::string>();
            if(encoding=="utf8") options.encoding=TextEncoding::Utf8;
            if(encoding=="utf16be") options.encoding=TextEncoding::Utf16BE;
            bool failed=false; std::vector<std::string> records;
            try{records=TextFileImporter::ReadUtf8Records(root/"fixtures"/"text_import"/item.at("file").get<std::string>(),options);}catch(const std::runtime_error&){failed=true;}
            if(item.contains("expected_error")) REQUIRE(failed);
            else { REQUIRE(!failed); REQUIRE(records==item.at("expected").get<std::vector<std::string>>()); }
        }
        std::cout << "PASS 15 external text import fixtures\n";
    } else std::cout << "External text fixtures: NOT_RUN\n";
#endif
}
void TestCompareBudgetAndCancellation() {
    CompareExecutionContext context;
    std::stop_source stop;context.stop=stop.get_token();
    context.progress=[&](std::size_t,std::size_t){stop.request_stop();};
    bool canceled=false;
    try{CompareService{}.Compare(std::vector<std::string>(1000,"same"),std::vector<std::string>(1000,"same"),SupportedMathOptions(),&context);}
    catch(const std::runtime_error& ex){canceled=std::string(ex.what())=="COMPARE_CANCELED";}
    REQUIRE(canceled); REQUIRE(context.used_bytes==8ull*1024*1024);
    REQUIRE(CompareService{}.Compare({"again"},{"again"},SupportedMathOptions()).at(0).status==CompareStatus::Ok);
    bool rejected=false; try{CharacterDiff{}.Diff(std::string(20000,'a'),std::string(20000,'b'));}catch(const std::runtime_error&){rejected=true;} REQUIRE(rejected);
    const std::string prefix(65535,'a');
    const auto diff=CharacterDiff{}.Diff(prefix+"b",prefix+"c");
    std::string joined;for(const auto& fragment:diff.reference_fragments) joined+=fragment.text; REQUIRE(joined==prefix+"b");
    CompareExecutionContext measured;
    CompareService{}.Compare(std::vector<std::string>(100,"same"),std::vector<std::string>(100,"same"),SupportedMathOptions(),&measured);
    REQUIRE(measured.used_bytes==8ull*1024*1024); REQUIRE(measured.peak_bytes>measured.used_bytes); REQUIRE(measured.peak_bytes<=CompareExecutionContext::budget_bytes);
}

void TestFourProfiles() {
    CompareService service;
    const auto strict=CompareService::Preset("strict_rows_v1");
    auto rows=service.Compare({"abc","","x"},{"ab","","totally different"},strict);
    REQUIRE(rows.size()==3); REQUIRE(rows[0].status==CompareStatus::Ng);
    REQUIRE(std::abs(rows[0].similarity-200.0/3)<1e-9);
    REQUIRE(rows[1].status==CompareStatus::Ok); REQUIRE(rows[2].status==CompareStatus::Ng);
    REQUIRE(service.Compare({"a","b"},{"a"},strict)[1].status==CompareStatus::Missing);
    REQUIRE(service.Compare({"a","b"},{"a",""},strict)[1].status==CompareStatus::Ng);
    REQUIRE(service.Compare({"-1"},{"1"},strict)[0].status==CompareStatus::Ng);
    REQUIRE(service.Compare({"e\xCC\x81"},{"é"},strict)[0].status==CompareStatus::Ng);
    auto joined=[](const auto& fragments){std::string out;for(const auto& f:fragments) out+=f.text;return out;};
    for(const auto& row:rows) {
        REQUIRE(joined(row.diff.reference_fragments)==row.reference_text);
        REQUIRE(joined(row.diff.actual_fragments)==row.actual_text);
    }
#ifdef ADAYO_HAS_UTF8PROC
    const auto cer=CompareService::Preset("asr_cer_v1");
    const auto total=CompareService::Totals(service.Compare({"a","123456789"},{"b","123456789"},cer));
    REQUIRE(total.reference_units==10); REQUIRE(std::abs(*total.ErrorRate()-0.1)<1e-9);
    auto relaxed=cer; relaxed.max_error_rate=3;
    REQUIRE(service.Compare({"a"},{"aaaa"},relaxed)[0].status==CompareStatus::Ok);
    REQUIRE(!service.Compare({""},{"abc"},relaxed)[0].error_rate);
    REQUIRE(service.Compare({""},{"abc"},relaxed)[0].status==CompareStatus::Ng);
    const auto tie=service.Compare({"ab"},{"ba"},cer)[0].edits;
    REQUIRE(tie.substitutions==2); REQUIRE(tie.deletions==0); REQUIRE(tie.insertions==0);
    REQUIRE(service.Compare({"-1"},{"1"},CompareService::Preset("legacy_v1"))[0].status==CompareStatus::Ok);
#endif
#ifdef ADAYO_HAS_JSON_CONFIG
    if(const auto* environment=std::getenv("ADAYO_REVIEW_WORKPACK")) {
        std::ifstream input(PathFromUtf8(environment)/"fixtures"/"compare_cases.json");
        const auto fixtures=nlohmann::json::parse(input);
        for(const auto& c:fixtures.at("score_cases")) {
            const auto row=service.Compare({c.at("left")},{c.at("right")},strict)[0];
            REQUIRE(std::abs(row.similarity-c.at("levenshtein_similarity").get<double>())<1e-9);
        }
#ifdef ADAYO_HAS_UTF8PROC
        for(const auto& c:fixtures.at("metric_cases")) {
            const auto row=service.Compare({c.at("left")},{c.at("right")},CompareService::Preset(c.at("profile").get<std::string>()))[0];
            const auto& e=c.at("expected");
            REQUIRE(row.edits.substitutions==e.at("S").get<std::size_t>());
            REQUIRE(row.edits.deletions==e.at("D").get<std::size_t>());
            REQUIRE(row.edits.insertions==e.at("I").get<std::size_t>());
            REQUIRE(row.edits.reference_units==e.at("N").get<std::size_t>());
            if(e.at("error_rate").is_null()) REQUIRE(!row.error_rate);
            else { REQUIRE(row.error_rate); REQUIRE(std::abs(*row.error_rate-e.at("error_rate").get<double>())<1e-9); }
            REQUIRE(joined(row.diff.reference_fragments)==row.reference_text);
            REQUIRE(joined(row.diff.actual_fragments)==row.actual_text);
        }
#endif
        for(const auto& c:fixtures.at("alignment_cases")) {
            auto options=SupportedMathOptions();
            if(c.at("mode")=="rows") options=CompareService::Preset("strict_rows_v1");
            options.alignment.alignment_threshold=c.value("alignment_threshold",80.0);
            options.alignment.anchor_threshold=c.value("anchor_threshold",95.0);
            const auto output=service.Compare(c.at("reference").get<std::vector<std::string>>(),c.at("actual").get<std::vector<std::string>>(),options);
            nlohmann::json pairs=nlohmann::json::array();
            for(const auto& row:output) {
                nlohmann::json pair=nlohmann::json::array();
                pair.push_back(row.reference_index?nlohmann::json(*row.reference_index):nlohmann::json(nullptr));
                pair.push_back(row.actual_index?nlohmann::json(*row.actual_index):nlohmann::json(nullptr));
                pairs.push_back(pair);
            }
            if(c.contains("expected_pairs")) REQUIRE(pairs==c.at("expected_pairs"));
            if(c.contains("expected_unordered_pair_set")) {
                auto expected=c.at("expected_unordered_pair_set");
                std::sort(pairs.begin(),pairs.end());std::sort(expected.begin(),expected.end());
                REQUIRE(pairs==expected);
            }
            AssertUniqueIndexes(output);
            if(c.contains("invariants")) {
                std::optional<std::size_t> r,a; std::size_t matches=0;
                for(const auto& row:output) {
                    if(row.reference_index) { if(r) REQUIRE(*r<*row.reference_index); r=row.reference_index; }
                    if(row.actual_index) { if(a) REQUIRE(*a<*row.actual_index); a=row.actual_index; }
                    if(row.reference_index && row.actual_index) ++matches;
                }
                REQUIRE(matches<2);
            }
        }
        std::cout<<"PASS external comparison goldens\n";
    }
#endif
}

int main() {
    return test::RunTestMain("adayo_p6_compare_tests", [] {
        TestStrictImportAndIndelGoldens();
        TestFourProfiles();
        TestCompareBudgetAndCancellation();
        TestFixedAlignmentSet();
        TestPunctuationSwitch();
        TestUnicodePunctuationWithUtf8proc();
        TestAnchorCannotBypassAlignmentThreshold();
        TestInvalidThresholdsRejected();
        TestCompareMatrixBudgetRejectsOversizeInput();
        TestTextImporterBomAndDelimiter();
        TestTextImporterUtf16Bom();
        TestTextImporterGb18030AndInvalidBytes();
        TestTextImporterSkipsUnicodeWhitespaceOnlyRecords();
        TestPerformance1000();
        TestPerformance5000WithRapidfuzz();
    });
}
