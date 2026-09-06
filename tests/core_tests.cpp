#include "core/compare/CharacterDiff.h"
#include "core/compare/TextNormalizer.h"
#include "core/compare/TextSimilarity.h"
#include "core/unicode/Utf8.h"
#include "core/workbook/ColumnAnalyzer.h"
#include "core/workbook/ViewBuilder.h"
#include "platform/UnicodePath.h"
#include "platform/FileIo.h"
#include "services/CompareService.h"
#include "services/CorpusViewService.h"

#include "TestCheck.h"

#include <cmath>
#include <filesystem>
#include <iostream>

using namespace adayo;

static void TestUtf8() {
    const std::string s = "中文 العربية English";
    REQUIRE(unicode::Encode(unicode::Decode(s)) == s);
}

static void TestSimilarityUsesCodepoints() {
    TextSimilarity sim;
    REQUIRE(std::abs(sim.Ratio("打开空调", "打开空调") - 100.0) < 0.001);
    const double score = sim.Ratio("打开空调", "关闭空调");
    REQUIRE(score > 0.0 && score < 100.0);
}

static void TestDiff() {
    CharacterDiff diff;
    const auto r = diff.Diff("温度设置为22度", "温度设置为23度");
    bool ref_changed = false, actual_changed = false;
    for (const auto& f : r.reference_fragments) if (f.kind == DiffKind::Changed && f.text.find("2") != std::string::npos) ref_changed = true;
    for (const auto& f : r.actual_fragments) if (f.kind == DiffKind::Changed && f.text.find("3") != std::string::npos) actual_changed = true;
    REQUIRE(ref_changed && actual_changed);
}

static void TestSequenceAlignmentMissing() {
    CompareService service;
    CompareOptions options;
    options.normalizer.normalization=UnicodeNormalization::None;
    options.normalizer.case_fold=false;
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
    REQUIRE(missing_close_ac);
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
    REQUIRE(result.view.rows.size() == 2);
    REQUIRE(result.view.rows[0][1] == "功能A");
    REQUIRE(result.view.rows[0][2] == "hello");
    REQUIRE(result.view.rows[1][2] == "world");
}

static void TestColumnGuess() {
    ColumnAnalyzer a;
    REQUIRE(a.GuessLanguage("英语说法举例") == "en-GB" || a.GuessLanguage("英语说法举例") == "en-US");
    REQUIRE(a.GuessLanguage("阿语测试结果") == "ar-SA");
    REQUIRE(a.GuessLanguage("ARG") == "ar-SA");
    REQUIRE(a.GuessType("中文说法举例") == SuggestedColumnType::Utterance);
    REQUIRE(a.GuessType("测试结果") == SuggestedColumnType::Result);
}

static void TestUnicodePathRoundTrip() {
    const std::filesystem::path path = PathFromUtf8("中文路径/日本語/한국어/العربية/空格 路径/(demo)/语料.xlsx");
    REQUIRE(PathFromUtf8(PathToUtf8(path)) == path);
}

static void TestPiperNfdNormalization() {
#ifdef ADAYO_HAS_UTF8PROC
    const auto composed = unicode::Encode(U"\u0439\u0457\u00e9");
    const auto decomposed = unicode::Encode(U"\u0438\u0306\u0456\u0308e\u0301");
    REQUIRE(unicode::NormalizeNfd(composed) == decomposed);
    REQUIRE(unicode::NormalizeNfd(decomposed) == decomposed);
    REQUIRE(unicode::NormalizeNfd("Upper, Lower!") == "Upper, Lower!");
    REQUIRE(unicode::NormalizeNfd("").empty());
#else
    bool blocked = false;
    try { unicode::NormalizeNfd("text"); } catch (const std::runtime_error&) { blocked = true; }
    REQUIRE(blocked);
#endif
    bool invalid = false;
    try { unicode::NormalizeNfd(std::string("a\0b", 3)); } catch (const std::runtime_error&) { invalid = true; }
    REQUIRE(invalid);
}

static void TestPlayEditInvalidatesOnlyEditedResult() {
    CorpusViewService service;
    auto session = service.CreateSession(
        {{"功能A", "hello\nworld", "你好\n世界"}},
        {
            {0, "功能", "A", ColumnRole::Reference, "", ""},
            {1, "英语", "B", ColumnRole::Play, "en-US", "sherpa-vits"},
            {2, "中文", "C", ColumnRole::Play, "zh-CN", "sherpa-vits"},
        });
    service.CycleResult(session, 0, 3);
    service.CycleResult(session, 1, 3);
    service.CycleResult(session, 0, 5);

    service.UpdateDisplayCell(session, 0, 2, "hello edited");

    REQUIRE(session.view.rows[0][3].empty());
    REQUIRE(session.view.rows[1][3] == "✔");
    REQUIRE(session.view.rows[0][5] == "✔");
}

static void TestReferenceEditInvalidatesRawRowResultsOnly() {
    CorpusViewService service;
    auto session = service.CreateSession(
        {{"功能A", "hello\nworld", "你好\n世界"}, {"功能B", "again", "再次"}},
        {
            {0, "功能", "A", ColumnRole::Reference, "", ""},
            {1, "英语", "B", ColumnRole::Play, "en-US", "sherpa-vits"},
            {2, "中文", "C", ColumnRole::Play, "zh-CN", "sherpa-vits"},
        });
    service.CycleResult(session, 0, 3);
    service.CycleResult(session, 1, 3);
    service.CycleResult(session, 0, 5);
    service.CycleResult(session, 1, 5);
    service.CycleResult(session, 2, 3);

    service.UpdateDisplayCell(session, 0, 1, "功能A 改");

    REQUIRE(session.view.rows[0][3].empty());
    REQUIRE(session.view.rows[0][5].empty());
    REQUIRE(session.view.rows[1][3].empty());
    REQUIRE(session.view.rows[1][5].empty());
    REQUIRE(session.view.rows[2][3] == "✔");
}

static void TestSessionRevisionAndExportIdentity() {
    CorpusViewService service;
    auto make=[&] { return service.CreateSession({{"reference","hello\nworld"}}, {
        {0,"Reference","A",ColumnRole::Reference,"",""},
        {1,"English","B",ColumnRole::Play,"en-US","sherpa-vits"}}); };
    auto session=make();
    REQUIRE(session.session_id!=0); REQUIRE(!session.Dirty());
    service.UpdateDisplayCell(session,0,2,"hello");
    REQUIRE(session.revision==0);
    const auto rows=session.view.rows;
    bool rejected=false;
    try { service.UpdateDisplayCell(session,0,0,"index"); } catch(const std::exception&) { rejected=true; }
    REQUIRE(rejected); REQUIRE(session.revision==0); REQUIRE(session.view.rows==rows);
    for(int i=0;i<5;++i) service.CycleResult(session,0,3);
    REQUIRE(session.revision==5); REQUIRE(session.Dirty());
    const auto snapshot_revision=session.revision;
    service.UpdateDisplayCell(session,0,2,"edited");
    REQUIRE(session.revision==6);
    REQUIRE(service.MarkExported(session,session.session_id,snapshot_revision));
    REQUIRE(session.exported_revision==5); REQUIRE(session.Dirty());
    REQUIRE(service.MarkExported(session,session.session_id,6)); REQUIRE(!session.Dirty());
    REQUIRE(service.MarkExported(session,session.session_id,5)); REQUIRE(session.exported_revision==6);
    auto next=make();
    REQUIRE(next.session_id!=session.session_id);
    service.CycleResult(next,0,3);
    REQUIRE(!service.MarkExported(next,session.session_id,6)); REQUIRE(next.Dirty());
    REQUIRE(!service.MarkExported(next,next.session_id,2)); REQUIRE(next.exported_revision==0);
    service.UpdateDisplayCell(next,0,1,"new reference");
    REQUIRE(next.revision==2); REQUIRE(next.view.rows[0][3].empty());
}

static void TestAtomicWriteFaults(const std::string& name, int case_index) {
    const auto root=test::IsolatedRoot()/("io-faults-"+std::to_string(case_index));
    std::filesystem::create_directory(root);
    const auto path=root/PathFromUtf8(name);
    const std::string original="{original}",replacement="{replacement}";
    WriteBinaryFileAtomically(path,original.data(),original.size());
    const auto hash=FileSha256(path);
    struct FaultOperations final:AtomicFileOperations {
        int fault{};
        std::size_t Write(std::FILE* file,const void* data,std::size_t size) override {
            return AtomicFileOperations::Write(file,data,fault==0?size/2:size);
        }
        bool Flush(std::FILE* file) override { const bool ok=AtomicFileOperations::Flush(file); return ok && fault!=1; }
        bool Close(std::FILE* file) override { const bool ok=AtomicFileOperations::Close(file); return ok && fault!=2; }
        void Replace(const std::filesystem::path& from,const std::filesystem::path& to) override {
            if(fault==3) throw std::runtime_error("Injected replace failure");
            AtomicFileOperations::Replace(from,to);
        }
    } operations;
    for(int fault=0;fault<4;++fault) {
        operations.fault=fault;
        bool rejected=false;
        try { WriteBinaryFileAtomically(path,replacement.data(),replacement.size(),operations); }
        catch(const std::runtime_error&) { rejected=true; }
        REQUIRE(rejected); REQUIRE(FileSha256(path)==hash);
        REQUIRE(std::distance(std::filesystem::directory_iterator(root),std::filesystem::directory_iterator{})==1);
    }
    struct Collision final:AtomicFileOperations {
        std::filesystem::path other;
        std::FILE* CreateExclusive(const std::filesystem::path& path) override {
            other=path;
            WriteBinaryFile(path,"OTHER",5);
            return AtomicFileOperations::CreateExclusive(path);
        }
    } collision;
    bool rejected=false;
    try{WriteBinaryFileAtomically(path,replacement.data(),replacement.size(),collision);}catch(const std::runtime_error&){rejected=true;}
    REQUIRE(rejected); REQUIRE(FileSha256(path)==hash);
    REQUIRE(std::filesystem::exists(collision.other));
    REQUIRE(FileSha256(collision.other)==Sha256("OTHER"));
    WriteBinaryFileAtomically(path,replacement.data(),replacement.size());
    REQUIRE(FileSha256(path)==Sha256(replacement));
}

int main() {
    return test::RunTestMain("adayo_core_tests", [] {
        TestUtf8();
        TestAtomicWriteFaults("config.json",0);
        TestAtomicWriteFaults(std::string(250,'x')+".json",1);
        std::string unicode_name;
        for(int i=0;i<80;++i) unicode_name+="界";
        TestAtomicWriteFaults(unicode_name+".json",2);
        TestSessionRevisionAndExportIdentity();
        TestSimilarityUsesCodepoints();
        TestDiff();
        TestSequenceAlignmentMissing();
        TestViewExpansion();
        TestColumnGuess();
        TestUnicodePathRoundTrip();
        TestPiperNfdNormalization();
        TestPlayEditInvalidatesOnlyEditedResult();
        TestReferenceEditInvalidatesRawRowResultsOnly();
    });
}
