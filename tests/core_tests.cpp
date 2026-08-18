#include "core/compare/CharacterDiff.h"
#include "core/compare/TextNormalizer.h"
#include "core/compare/TextSimilarity.h"
#include "core/unicode/Utf8.h"
#include "core/workbook/ColumnAnalyzer.h"
#include "core/workbook/ViewBuilder.h"
#include "services/CompareService.h"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace adayo;

static void TestUtf8() {
    const std::string s = "中文 العربية English";
    assert(unicode::Encode(unicode::Decode(s)) == s);
}

static void TestSimilarityUsesCodepoints() {
    TextSimilarity sim;
    assert(std::abs(sim.Ratio("打开空调", "打开空调") - 100.0) < 0.001);
    const double score = sim.Ratio("打开空调", "关闭空调");
    assert(score > 0.0 && score < 100.0);
}

static void TestDiff() {
    CharacterDiff diff;
    const auto r = diff.Diff("温度设置为22度", "温度设置为23度");
    bool ref_changed = false, actual_changed = false;
    for (const auto& f : r.reference_fragments) if (f.kind == DiffKind::Changed && f.text.find("2") != std::string::npos) ref_changed = true;
    for (const auto& f : r.actual_fragments) if (f.kind == DiffKind::Changed && f.text.find("3") != std::string::npos) actual_changed = true;
    assert(ref_changed && actual_changed);
}

static void TestSequenceAlignmentMissing() {
    CompareService service;
    CompareOptions options;
    options.alignment.alignment_threshold = 75.0;
    options.alignment.anchor_threshold = 95.0;
    options.pass_threshold = 100.0;
    const std::vector<std::string> ref = {"打开空调", "关闭空调", "打开车窗", "关闭车窗"};
    const std::vector<std::string> act = {"打开空调", "打开车窗", "关闭车窗"};
    const auto rows = service.Compare(ref, act, options);
    bool missing_close_ac = false;
    for (const auto& row : rows) {
        if (row.status == CompareStatus::Missing && row.reference_text == "关闭空调") missing_close_ac = true;
    }
    assert(missing_close_ac);
}

static void TestViewExpansion() {
    ViewBuilder b;
    std::vector<std::vector<std::string>> raw = {{"功能A", "hello\nworld", "你好"}};
    std::vector<SelectedColumn> columns = {
        {0, "功能", "A", ColumnRole::Reference, "", ""},
        {1, "英语", "B", ColumnRole::Play, "en-US", "sherpa-vits"},
        {2, "中文", "C", ColumnRole::Play, "zh-CN", "sherpa-vits"},
    };
    const auto result = b.Build(raw, columns);
    assert(result.view.rows.size() == 2);
    assert(result.view.rows[0][1] == "功能A");
    assert(result.view.rows[0][2] == "hello");
    assert(result.view.rows[1][2] == "world");
}

static void TestColumnGuess() {
    ColumnAnalyzer a;
    assert(a.GuessLanguage("英语说法举例") == "en-GB" || a.GuessLanguage("英语说法举例") == "en-US");
    assert(a.GuessLanguage("阿语测试结果") == "ar-SA");
    assert(a.GuessLanguage("ARG") == "ar-SA");
    assert(a.GuessType("中文说法举例") == SuggestedColumnType::Utterance);
    assert(a.GuessType("测试结果") == SuggestedColumnType::Result);
}

int main() {
    TestUtf8();
    TestSimilarityUsesCodepoints();
    TestDiff();
    TestSequenceAlignmentMissing();
    TestViewExpansion();
    TestColumnGuess();
    std::cout << "adayo_core_tests: PASS\n";
    return 0;
}
