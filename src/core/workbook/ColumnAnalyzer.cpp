#include "core/workbook/ColumnAnalyzer.h"

#include "core/unicode/Utf8.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace adayo {
namespace {
std::string Trim(const std::string& s) {
    return unicode::Encode(unicode::Trim(unicode::Decode(s)));
}

std::string AsciiUpperCompact(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalpha(c)) out.push_back(static_cast<char>(std::toupper(c)));
    }
    return out;
}

const std::unordered_map<std::string, std::string> kTokenAliases = {
    {"ZH","zh-CN"},{"ZHCN","zh-CN"},{"ZHS","zh-CN"},{"CHS","zh-CN"},{"CHI","zh-CN"},{"CN","zh-CN"},{"CNS","zh-CN"},
    {"CHT","zh-TW"},{"ZHT","zh-TW"},{"TWN","zh-TW"},{"TW","zh-TW"},
    {"EN","en-US"},{"ENU","en-US"},{"ENUS","en-US"},{"AMENG","en-US"},{"USEN","en-US"},
    {"ENG","en-GB"},{"ENGB","en-GB"},{"ENGUK","en-GB"},{"UKEN","en-GB"},{"ENUK","en-GB"},
    {"FR","fr-FR"},{"FRA","fr-FR"},{"FRE","fr-FR"},{"FRF","fr-FR"},
    {"DE","de-DE"},{"DEU","de-DE"},{"GER","de-DE"},{"DEG","de-DE"},
    {"IT","it-IT"},{"ITA","it-IT"},
    {"ES","es-ES"},{"ESP","es-ES"},{"SPA","es-ES"},{"SPAIN","es-ES"},{"SPM","es-ES"},{"ESN","es-ES"},{"CAST","es-ES"},{"ARG","es-ES"},{"LATAM","es-ES"},
    {"PT","pt-PT"},{"POR","pt-PT"},{"PTG","pt-PT"},{"PTPT","pt-PT"},{"PTB","pt-BR"},{"BRP","pt-BR"},{"PTBR","pt-BR"},{"BR","pt-BR"},
    {"AR","ar-SA"},{"ARA","ar-SA"},{"ARB","ar-SA"},
    {"TH","th-TH"},{"THA","th-TH"},{"JA","ja-JP"},{"JP","ja-JP"},{"JPN","ja-JP"},
    {"KO","ko-KR"},{"KR","ko-KR"},{"KOR","ko-KR"},{"RU","ru-RU"},{"RUS","ru-RU"},
    {"VI","vi-VN"},{"VIE","vi-VN"},{"VN","vi-VN"},{"ID","id-ID"},{"IND","id-ID"},{"IDN","id-ID"},
    {"MS","ms-MY"},{"MAY","ms-MY"},{"MALAY","ms-MY"},{"TR","tr-TR"},{"TUR","tr-TR"},
    {"NL","nl-NL"},{"NLD","nl-NL"},{"DUT","nl-NL"},{"PL","pl-PL"},{"POL","pl-PL"},
    {"CS","cs-CZ"},{"CZE","cs-CZ"},{"CES","cs-CZ"},{"HU","hu-HU"},{"HUN","hu-HU"},
    {"RO","ro-RO"},{"RON","ro-RO"},{"SV","sv-SE"},{"SWE","sv-SE"},{"DA","da-DK"},{"DAN","da-DK"},
    {"FI","fi-FI"},{"FIN","fi-FI"},{"NO","no-NO"},{"NOR","no-NO"},{"EL","el-GR"},{"GRE","el-GR"},{"ELL","el-GR"},
    {"HE","he-IL"},{"HEB","he-IL"},{"HI","hi-IN"},{"HIN","hi-IN"},{"BN","bn-BD"},{"BEN","bn-BD"},
    {"UR","ur-PK"},{"URD","ur-PK"},{"FA","fa-IR"},{"FAS","fa-IR"},{"PER","fa-IR"},{"UK","uk-UA"},{"UKR","uk-UA"},
    {"KM","km-KH"},{"KHM","km-KH"},{"KHMER","km-KH"}
};

const std::vector<std::pair<std::string, std::string>> kChinesePrefixes = {
    {"英语","en-GB"},{"英文","en-GB"},{"中文","zh-CN"},{"阿语","ar-SA"},{"阿拉","ar-SA"},{"西语","es-ES"},{"西班","es-ES"},
    {"德语","de-DE"},{"法语","fr-FR"},{"意语","it-IT"},{"意大","it-IT"},{"柬埔","km-KH"},{"泰语","th-TH"},{"日语","ja-JP"},
    {"韩语","ko-KR"},{"俄语","ru-RU"},{"葡语","pt-PT"}
};

const std::unordered_map<std::string, std::string> kExactAliases = {
    {"中文","zh-CN"},{"中文说法举例","zh-CN"},{"英语","en-US"},{"英文","en-US"},{"英语说法举例","en-US"},
    {"阿语","ar-SA"},{"阿语说法举例","ar-SA"},{"阿拉伯语","ar-SA"},{"西语","es-ES"},{"西语说法举例","es-ES"},
    {"西班牙语","es-ES"},{"德语","de-DE"},{"德语说法举例","de-DE"},{"法语","fr-FR"},{"法语说法举例","fr-FR"},
    {"意大利语","it-IT"},{"意大利语说法举例","it-IT"},{"柬埔寨语","km-KH"},{"柬埔寨语说法举例","km-KH"},{"Khmer","km-KH"}
};

bool Contains(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}
}

const std::vector<LanguageOption>& ColumnAnalyzer::Languages() {
    static const std::vector<LanguageOption> languages = {
        {"","未指定"},{"zh-CN","中文（简体）"},{"zh-TW","中文（繁体）"},{"en-US","英语（美国）"},{"en-GB","英语（英国）"},
        {"ja-JP","日语"},{"ko-KR","韩语"},{"de-DE","德语"},{"fr-FR","法语"},{"it-IT","意大利语"},{"es-ES","西班牙语"},
        {"pt-PT","葡萄牙语"},{"pt-BR","葡萄牙语（巴西）"},{"ru-RU","俄语"},{"ar-SA","阿拉伯语"},{"th-TH","泰语"},
        {"vi-VN","越南语"},{"id-ID","印尼语"},{"ms-MY","马来语"},{"tr-TR","土耳其语"},{"nl-NL","荷兰语"},{"pl-PL","波兰语"},
        {"cs-CZ","捷克语"},{"hu-HU","匈牙利语"},{"ro-RO","罗马尼亚语"},{"sv-SE","瑞典语"},{"da-DK","丹麦语"},{"fi-FI","芬兰语"},
        {"no-NO","挪威语"},{"el-GR","希腊语"},{"he-IL","希伯来语"},{"hi-IN","印地语"},{"bn-BD","孟加拉语"},{"ur-PK","乌尔都语"},
        {"fa-IR","波斯语"},{"uk-UA","乌克兰语"},{"km-KH","柬埔寨语"}
    };
    return languages;
}

std::string ColumnAnalyzer::ExcelColumnName(std::size_t zero_based_index) {
    std::size_t n = zero_based_index + 1;
    std::string name;
    while (n > 0) {
        const std::size_t rem = (n - 1) % 26;
        name.insert(name.begin(), static_cast<char>('A' + rem));
        n = (n - 1) / 26;
    }
    return name;
}

std::string ColumnAnalyzer::GuessLanguage(const std::string& raw_header) const {
    const std::string h = Trim(raw_header);
    if (h.empty()) return {};

    auto exact = kExactAliases.find(h);
    if (exact != kExactAliases.end()) return exact->second;

    const std::string compact = AsciiUpperCompact(h);
    if (!compact.empty()) {
        auto token = kTokenAliases.find(compact);
        if (token != kTokenAliases.end()) return token->second;
    }

    for (const auto& [prefix, lang] : kChinesePrefixes) {
        if (h.rfind(prefix, 0) == 0) return lang;
    }

    const std::vector<std::pair<std::string, std::string>> one_char = {
        {"中","zh-CN"},{"英","en-GB"},{"阿","ar-SA"},{"西","es-ES"},{"德","de-DE"},{"法","fr-FR"},{"意","it-IT"},
        {"柬","km-KH"},{"泰","th-TH"},{"日","ja-JP"},{"韩","ko-KR"},{"俄","ru-RU"},{"葡","pt-PT"}
    };
    for (const auto& [prefix, lang] : one_char) {
        if (h.rfind(prefix, 0) == 0) return lang;
    }

    for (const auto& [alias, lang] : kExactAliases) {
        if (alias != "说法举例" && Contains(h, alias)) return lang;
    }
    return {};
}

SuggestedColumnType ColumnAnalyzer::GuessType(const std::string& header) const {
    const std::string h = Trim(header);
    if (h.empty()) return SuggestedColumnType::Unknown;
    if (Contains(h, "测试结果") || h == "result" || h == "results" || h == "Result" || h == "Results") {
        return SuggestedColumnType::Result;
    }
    if (Contains(h, "说法举例")) return SuggestedColumnType::Utterance;

    static const std::unordered_set<std::string> meta = {
        "二级功能","三级功能","属性","描述","讯飞支持说明","service","semantic","operation"
    };
    if (meta.contains(h)) return SuggestedColumnType::Meta;
    if (!GuessLanguage(h).empty()) return SuggestedColumnType::Utterance;
    return SuggestedColumnType::Unknown;
}

std::vector<ColumnProfile> ColumnAnalyzer::Analyze(
    const std::vector<std::string>& headers,
    const std::vector<std::vector<std::string>>& rows) const {

    std::vector<ColumnProfile> out;
    for (std::size_t c = 0; c < headers.size(); ++c) {
        ColumnProfile p;
        p.source_index = c;
        p.excel_column = ExcelColumnName(c);
        p.header = Trim(headers[c]);
        for (const auto& row : rows) {
            if (c >= row.size()) continue;
            const auto value = Trim(row[c]);
            if (value.empty()) continue;
            ++p.non_empty_count;
            if (p.samples.size() < 5) p.samples.push_back(value);
        }
        if (p.header.empty() && p.non_empty_count == 0) continue;
        if (p.header.empty()) p.header = "未命名列" + std::to_string(c);
        p.suggested_type = GuessType(p.header);
        p.guessed_language = GuessLanguage(p.header);
        p.language_code = p.guessed_language;
        p.selected = p.suggested_type == SuggestedColumnType::Utterance || p.suggested_type == SuggestedColumnType::Meta;
        p.role = p.suggested_type == SuggestedColumnType::Utterance ? ColumnRole::Play :
                 p.suggested_type == SuggestedColumnType::Meta ? ColumnRole::Reference : ColumnRole::Ignore;
        p.tts_engine_id = "sherpa-vits";
        out.push_back(std::move(p));
    }
    return out;
}

} // namespace adayo
