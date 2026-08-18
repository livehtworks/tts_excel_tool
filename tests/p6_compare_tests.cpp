#include "services/CompareService.h"

#include "TestCheck.h"

#include <chrono>
#include <iostream>
#include <set>

using namespace adayo;

namespace {
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
    CompareService service;
    CompareOptions options;
    options.alignment.alignment_threshold = 75.0;
    options.alignment.anchor_threshold = 95.0;
    options.pass_threshold = 100.0;
    options.normalizer.ignore_punctuation = true;
    options.normalizer.case_fold = true;
    options.normalizer.unicode_nfkc = true;

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
    CompareOptions loose;
    loose.normalizer.ignore_punctuation = true;
    loose.pass_threshold = 100.0;
    auto rows = service.Compare({"hello!"}, {"hello"}, loose);
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].status == CompareStatus::Ok);

    CompareOptions strict;
    strict.normalizer.ignore_punctuation = false;
    strict.pass_threshold = 100.0;
    rows = service.Compare({"hello!"}, {"hello"}, strict);
    REQUIRE(rows[0].status == CompareStatus::Ng);
}

void TestPerformance1000() {
    CompareService service;
    CompareOptions options;
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
} // namespace

int main() {
    return test::RunTestMain("adayo_p6_compare_tests", [] {
        TestFixedAlignmentSet();
        TestPunctuationSwitch();
        TestPerformance1000();
    });
}
