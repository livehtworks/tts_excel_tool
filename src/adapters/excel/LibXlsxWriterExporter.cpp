#include "adapters/excel/LibXlsxWriterExporter.h"

#include "platform/FileIo.h"
#include "platform/UnicodePath.h"
#include "services/CompareService.h"
#include "core/unicode/Utf8.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <cmath>
#include <iomanip>
#include <sstream>

#ifdef ADAYO_HAS_LIBXLSXWRITER
#include <xlsxwriter.h>
#endif

namespace adayo {
#ifdef ADAYO_HAS_LIBXLSXWRITER
namespace {
const char* StatusText(CompareStatus status) {
    switch (status) {
        case CompareStatus::Ok: return "OK";
        case CompareStatus::Ng: return "NG";
        case CompareStatus::Missing: return "MISSING";
        case CompareStatus::Extra: return "EXTRA";
    }
    return "NG";
}

void Check(lxw_error error, const char* operation) {
    if (error != LXW_NO_ERROR) {
        throw std::runtime_error(std::string(operation) + " failed: " + lxw_strerror(error));
    }
}

void CheckString(const std::string& text) {
    std::size_t units=0;
    for(auto cp:unicode::DecodeStrict(text)) {
        units+=cp>0xffff?2:1;
        if(units>32767) throw std::invalid_argument("Excel cell exceeds 32767 UTF-16 units");
    }
}
void CheckDimensions(std::size_t rows,std::size_t columns) {
    if(rows>1048576 || columns>16384) throw std::invalid_argument("Excel row/column limit exceeded");
}
using Table=std::vector<std::vector<std::string>>;
void CheckTable(const Table& table) {
    CheckDimensions(table.size(),0);
    for(const auto& row:table) {
        CheckDimensions(0,row.size());
        for(const auto& text:row) CheckString(text);
    }
}
std::string Number(double value) {
    std::ostringstream out; out<<std::setprecision(17)<<value; return out.str();
}
void WriteTable(lxw_workbook* workbook,const char* name,const Table& table) {
    auto* sheet=workbook_add_worksheet(workbook,name);
    if(!sheet) throw std::runtime_error("Cannot create metadata worksheet");
    for(std::size_t r=0;r<table.size();++r)
        for(std::size_t c=0;c<table[r].size();++c)
            Check(worksheet_write_string(sheet,static_cast<lxw_row_t>(r),static_cast<lxw_col_t>(c),table[r][c].c_str(),nullptr),"write metadata");
    worksheet_freeze_panes(sheet,1,0);
    Check(worksheet_set_column(sheet,0,20,24,nullptr),"metadata width");
}
std::string NormalizationName(UnicodeNormalization value) {
    return value==UnicodeNormalization::None?"None":value==UnicodeNormalization::Nfc?"NFC":"NFKC";
}
Table ComparisonParameters(const std::vector<CompareReportGroup>& groups) {
    Table table={{"group","parameter","value"}};
    for(const auto& g:groups) {
        const auto& o=g.options;
        auto add=[&](const char* key,std::string value){table.push_back({g.label,key,std::move(value)});};
        add("profile_id",o.profile_id); add("custom",o.custom?"true":"false");
        add("reference_path",g.reference_source.path); add("reference_sha256",g.reference_source.sha256);
        add("actual_path",g.actual_source.path); add("actual_sha256",g.actual_source.sha256);
        add("normalizer_version","unicode_recipe_v1"); add("normalization",NormalizationName(o.normalizer.normalization));
        add("case_fold",o.normalizer.case_fold?"true":"false"); add("ignore_punctuation",o.normalizer.ignore_punctuation?"true":"false");
        add("collapse_whitespace",o.normalizer.collapse_whitespace?"true":"false"); add("trim",o.normalizer.trim?"true":"false");
        add("delimiter_literal",o.delimiter);
        add("empty_records",o.profile_id=="legacy_v1"?"skip_empty":"preserve_internal_no_phantom_terminal");
        add("pairing",o.pairing==CompareAlignment::Rows?"rows":"monotonic_indel_candidates");
        add("metric_id",CompareService::MetricId(o.metric)); add("numeric_column",CompareService::ValueLabel(o.metric));
        add("alignment_threshold",Number(o.alignment.alignment_threshold)); add("anchor_threshold",Number(o.alignment.anchor_threshold));
        add("anchor_uniqueness_margin",Number(o.alignment.anchor_uniqueness_margin)); add("gap_penalty",Number(o.alignment.gap_penalty));
        add("pass_threshold",Number(o.pass_threshold)); add("max_error_rate_fraction",Number(o.max_error_rate));
        add("pass_condition",o.metric==CompareMetric::Indel?"Indel >= pass_threshold":o.metric==CompareMetric::Exact?"raw Unicode equality":"defined error_rate <= max_error_rate");
        add("edit_tie_policy","unit_v1: match > substitution > deletion > insertion");
        add("token_recipe",o.metric==CompareMetric::Wer?"declared normalization/casefold, split Unicode whitespace; no Chinese segmenter":"Unicode code points");
        add("metric_scope",o.pairing==CompareAlignment::Sequence && (o.metric==CompareMetric::Cer || o.metric==CompareMetric::Wer)?"自动句对齐后 CER/WER":"paired records");
        add("diff_recipe","raw Unicode LCS; normalized OK may still have red original differences");
    }
    return table;
}
Table ComparisonStatistics(const std::vector<CompareReportGroup>& groups) {
    Table table={{"group","report_row","reference_record_1based","actual_record_1based","raw_reference_length","raw_actual_length","S","D","I","N","error_rate_fraction","status"}};
    for(const auto& g:groups) {
        for(std::size_t r=0;r<g.rows.size();++r) {
            const auto& row=g.rows[r]; const auto& e=row.edits;
            const bool measured=row.metric!=CompareMetric::Indel;
            table.push_back({g.label,std::to_string(r+1),row.reference_index?std::to_string(*row.reference_index+1):"",
                row.actual_index?std::to_string(*row.actual_index+1):"",std::to_string(row.reference_length),std::to_string(row.actual_length),
                measured?std::to_string(e.substitutions):"N/A",measured?std::to_string(e.deletions):"N/A",
                measured?std::to_string(e.insertions):"N/A",measured?std::to_string(e.reference_units):"N/A",
                measured?(row.error_rate?Number(*row.error_rate):"undefined (N=0)"):"N/A",StatusText(row.status)});
        }
        const auto e=CompareService::Totals(g.rows);
        const bool measured=g.options.metric!=CompareMetric::Indel;
        table.push_back({g.label,"TOTAL","","","","",measured?std::to_string(e.substitutions):"N/A",measured?std::to_string(e.deletions):"N/A",
            measured?std::to_string(e.insertions):"N/A",measured?std::to_string(e.reference_units):"N/A",
            measured?(e.ErrorRate()?Number(*e.ErrorRate()):"undefined (N=0)"):"N/A","weighted sum(errors)/sum(N)"});
    }
    return table;
}

void WriteRichFragments(lxw_worksheet* ws,
                        lxw_row_t row,
                        lxw_col_t col,
                        const std::vector<DiffFragment>& fragments,
                        lxw_format* changed_format,
                        lxw_format* cell_format) {
    if (fragments.empty()) {
        Check(worksheet_write_blank(ws, row, col, cell_format), "worksheet_write_blank");
        return;
    }
    if (fragments.size() == 1) {
        lxw_format* f = fragments.front().kind == DiffKind::Changed ? changed_format : cell_format;
        Check(worksheet_write_string(ws, row, col, fragments.front().text.c_str(), f), "worksheet_write_string");
        return;
    }

    std::vector<lxw_rich_string_tuple> tuples(fragments.size());
    std::vector<lxw_rich_string_tuple*> pointers;
    pointers.reserve(fragments.size() + 1);
    for (std::size_t i = 0; i < fragments.size(); ++i) {
        tuples[i].format = fragments[i].kind == DiffKind::Changed ? changed_format : nullptr;
        tuples[i].string = const_cast<char*>(fragments[i].text.c_str());
        pointers.push_back(&tuples[i]);
    }
    pointers.push_back(nullptr);
    Check(worksheet_write_rich_string(ws, row, col, pointers.data(), cell_format), "worksheet_write_rich_string");
}

struct WorkbookGuard {
    lxw_workbook* wb{};
    std::filesystem::path final_path;
    const char* output_buffer{};
    size_t output_buffer_size{};
    bool closed{false};

    WorkbookGuard() = default;
    WorkbookGuard(lxw_workbook* workbook, std::filesystem::path final, const char* buffer, size_t buffer_size)
        : wb(workbook), final_path(std::move(final)), output_buffer(buffer), output_buffer_size(buffer_size) {}
    WorkbookGuard(const WorkbookGuard&) = delete;
    WorkbookGuard& operator=(const WorkbookGuard&) = delete;
    WorkbookGuard(WorkbookGuard&& other) noexcept
        : wb(other.wb),
          final_path(std::move(other.final_path)),
          output_buffer(other.output_buffer),
          output_buffer_size(other.output_buffer_size),
          closed(other.closed) {
        other.wb = nullptr;
        other.output_buffer = nullptr;
        other.output_buffer_size = 0;
        other.closed = true;
    }
    WorkbookGuard& operator=(WorkbookGuard&& other) noexcept {
        if (this == &other) return *this;
        wb = other.wb;
        final_path = std::move(other.final_path);
        output_buffer = other.output_buffer;
        output_buffer_size = other.output_buffer_size;
        closed = other.closed;
        other.wb = nullptr;
        other.output_buffer = nullptr;
        other.output_buffer_size = 0;
        other.closed = true;
        return *this;
    }

    ~WorkbookGuard() {
        if (wb && !closed) workbook_close(wb);
        if (output_buffer) {
            std::free(const_cast<char*>(output_buffer));
        }
    }

    void Close() {
        const lxw_error close_error = workbook_close(wb);
        wb = nullptr;
        closed = true;
        Check(close_error, "workbook_close");
        if (!output_buffer || output_buffer_size == 0) {
            throw std::runtime_error("workbook_close produced empty output buffer");
        }
        WriteBinaryFileAtomically(final_path, output_buffer, output_buffer_size);
    }
};

std::unique_ptr<WorkbookGuard> CreateWorkbook(const std::filesystem::path& output) {
    auto guard = std::make_unique<WorkbookGuard>();
    guard->final_path = output;
    lxw_workbook_options options{};
    options.output_buffer = &guard->output_buffer;
    options.output_buffer_size = &guard->output_buffer_size;
    guard->wb = workbook_new_opt(nullptr, &options);
    if (!guard->wb) throw std::runtime_error("workbook_new 失败");
    return guard;
}
}
#endif

void LibXlsxWriterExporter::ExportRuntimeView(const RuntimeView& view, const std::filesystem::path& output) {
#ifdef ADAYO_HAS_LIBXLSXWRITER
    if(!view.source.path.empty()) {
        const auto source=PathFromUtf8(view.source.path);
        if(std::filesystem::weakly_canonical(source)==std::filesystem::weakly_canonical(output) ||
            (std::filesystem::exists(source) && std::filesystem::exists(output) && std::filesystem::equivalent(source,output)))
            throw std::invalid_argument("Cannot overwrite the original workbook");
    }
    CheckDimensions(CompareExecutionContext::Add(view.rows.size(),1),view.headers.size());
    CheckTable({view.headers}); CheckTable(view.rows);
    if(view.columns.size()!=view.headers.size() || view.row_meta.size()!=view.rows.size())
        throw std::invalid_argument("Runtime provenance dimensions do not match the view");
    CheckDimensions(CompareExecutionContext::Add(CompareExecutionContext::Multiply(view.rows.size(),view.columns.size()),1),11);
    Table source={{"parameter","value"},{"source_path",view.source.path},{"source_identity",view.source.identity},
        {"source_sha256",view.source.sha256},{"sheet",view.source.sheet},{"header_row",std::to_string(view.source.header_row)},
        {"imported_at",view.source.imported_at},{"coordinate_recipe","Excel rows/columns 1-based; raw row and expanded index 0-based"}};
    for(const auto& diagnostic:view.diagnostics) source.push_back({"diagnostic",diagnostic});
    Table mapping={{"report_row","report_column","raw_row_index","expanded_index","source_excel_row","source_excel_column",
        "source_cell","reference_owner_cell","segment_index","role","language"}};
    for(std::size_t r=0;r<view.rows.size();++r) {
        const auto& meta=view.row_meta[r];
        for(std::size_t c=0;c<view.columns.size();++c) {
            const auto& column=view.columns[c];
            const auto segment=meta.segment_indexes.find(column.source_index);
            const bool sourced=column.role!=ColumnRole::Index && meta.source_excel_row!=0;
            mapping.push_back({std::to_string(r+2),std::to_string(c+1),std::to_string(meta.raw_row_index),std::to_string(meta.expanded_index),
                sourced?std::to_string(meta.source_excel_row):"",column.excel_column,
                sourced?column.excel_column+std::to_string(meta.source_excel_row):"",
                column.role==ColumnRole::Reference && meta.reference_owner_excel_row?column.excel_column+std::to_string(*meta.reference_owner_excel_row):"",
                segment!=meta.segment_indexes.end() && segment->second?std::to_string(*segment->second):"",
                column.role==ColumnRole::Index?"index":column.role==ColumnRole::Reference?"reference":column.role==ColumnRole::Play?"play":"derived_result",column.language_code});
        }
    }
    CheckTable(source); CheckTable(mapping);
    auto guard = CreateWorkbook(output);
    lxw_worksheet* ws = workbook_add_worksheet(guard->wb, "运行视图");
    if (!ws) throw std::runtime_error("workbook_add_worksheet 失败");

    lxw_format* header = workbook_add_format(guard->wb);
    format_set_bold(header);
    format_set_text_wrap(header);
    lxw_format* wrap = workbook_add_format(guard->wb);
    format_set_text_wrap(wrap);

    for (lxw_col_t c = 0; c < view.headers.size(); ++c) {
        Check(worksheet_write_string(ws, 0, c, view.headers[c].c_str(), header), "write header");
    }
    for (lxw_row_t r = 0; r < view.rows.size(); ++r) {
        for (lxw_col_t c = 0; c < view.headers.size(); ++c) {
            const std::string value = c < view.rows[r].size() ? view.rows[r][c] : std::string{};
            Check(worksheet_write_string(ws, r + 1, c, value.c_str(), wrap), "write runtime cell");
        }
    }
    worksheet_freeze_panes(ws, 1, 0);
    if (!view.headers.empty()) worksheet_autofilter(ws, 0, 0, static_cast<lxw_row_t>(view.rows.size()), static_cast<lxw_col_t>(view.headers.size() - 1));
    for (lxw_col_t c = 0; c < view.headers.size(); ++c) worksheet_set_column(ws, c, c, c == 0 ? 8.0 : 28.0, nullptr);

    WriteTable(guard->wb,"来源",source);
    WriteTable(guard->wb,"坐标映射",mapping);
    guard->Close();
#else
    (void)view; (void)output;
    throw std::runtime_error("当前构建未启用 libxlsxwriter");
#endif
}

void LibXlsxWriterExporter::ExportComparison(const std::vector<CompareRow>& rows, const std::filesystem::path& output) {
#ifdef ADAYO_HAS_LIBXLSXWRITER
    ExportComparisonGroups({CompareReportGroup{"", rows}}, output);
#else
    (void)rows; (void)output;
    throw std::runtime_error("当前构建未启用 libxlsxwriter");
#endif
}

void LibXlsxWriterExporter::ExportComparisonGroups(const std::vector<CompareReportGroup>& groups, const std::filesystem::path& output) {
#ifdef ADAYO_HAS_LIBXLSXWRITER
    if (groups.empty()) {
        throw std::invalid_argument("对比报告没有可导出的语言组");
    }
    CheckDimensions(1,CompareExecutionContext::Multiply(groups.size(),4));
    std::size_t statistics_rows=1;
    for(const auto& g:groups) {
        CompareService::ValidateOptions(g.options);
        CheckString(g.label);
        CheckDimensions(CompareExecutionContext::Add(g.rows.size(),1),4);
        statistics_rows=CompareExecutionContext::Add(statistics_rows,CompareExecutionContext::Add(g.rows.size(),1));
        for(const auto& row:g.rows) {
            if(row.metric!=g.options.metric) throw std::invalid_argument("Report row metric does not match group options");
            CheckString(row.reference_text); CheckString(row.actual_text);
            if(!std::isfinite(row.similarity) || (row.error_rate && !std::isfinite(*row.error_rate)))
                throw std::invalid_argument("Report has nonfinite metric");
            if(row.reference_index && row.actual_index) {
                std::string ref,act;
                for(const auto& f:row.diff.reference_fragments) { CheckString(f.text); ref+=f.text; }
                for(const auto& f:row.diff.actual_fragments) { CheckString(f.text); act+=f.text; }
                if(ref!=row.reference_text || act!=row.actual_text) throw std::invalid_argument("Rich text fragments do not reconstruct the original");
            }
        }
    }
    CheckDimensions(statistics_rows,12);
    const auto parameters=ComparisonParameters(groups), statistics=ComparisonStatistics(groups);
    CheckTable(parameters); CheckTable(statistics);
    auto guard = CreateWorkbook(output);
    lxw_worksheet* ws = workbook_add_worksheet(guard->wb, "文本对比");
    if (!ws) throw std::runtime_error("workbook_add_worksheet 失败");

    lxw_format* header = workbook_add_format(guard->wb);
    format_set_bold(header);
    format_set_text_wrap(header);
    lxw_format* wrap = workbook_add_format(guard->wb);
    format_set_text_wrap(wrap);
    lxw_format* changed = workbook_add_format(guard->wb);
    format_set_font_color(changed, LXW_COLOR_RED);

    std::size_t max_rows = 0;
    for (const auto& group : groups) {
        max_rows = (std::max)(max_rows, group.rows.size());
    }

    const char* headers[] = {"正式文本", "机器文本", "相似度", "结果"};
    for (std::size_t g = 0; g < groups.size(); ++g) {
        const lxw_col_t base = static_cast<lxw_col_t>(g * 4);
        const auto prefix = groups[g].label.empty() ? std::string{} : groups[g].label + " ";
        for (lxw_col_t c = 0; c < 4; ++c) {
            const auto title=prefix+(c==2?CompareService::ValueLabel(groups[g].options.metric):std::string(headers[c]));
            CheckString(title);
            Check(worksheet_write_string(ws, 0, base + c, title.c_str(), header), "write compare header");
        }
        for (lxw_row_t r = 0; r < groups[g].rows.size(); ++r) {
            const auto& item = groups[g].rows[r];
            if (item.reference_index && item.actual_index) {
                WriteRichFragments(ws, r + 1, base + 0, item.diff.reference_fragments, changed, wrap);
                WriteRichFragments(ws, r + 1, base + 1, item.diff.actual_fragments, changed, wrap);
            } else {
                Check(worksheet_write_string(ws, r + 1, base + 0, item.reference_text.c_str(), wrap), "write reference");
                Check(worksheet_write_string(ws, r + 1, base + 1, item.actual_text.c_str(), wrap), "write actual");
            }
            if(item.metric==CompareMetric::Cer || item.metric==CompareMetric::Wer) {
                if(item.error_rate) Check(worksheet_write_number(ws,r+1,base+2,*item.error_rate*100,nullptr),"write error rate");
                else {
                    const auto text="N=0，错误率未定义；插入"+std::to_string(item.edits.insertions);
                    Check(worksheet_write_string(ws,r+1,base+2,text.c_str(),wrap),"write undefined error rate");
                }
            } else Check(worksheet_write_number(ws, r + 1, base + 2, item.similarity, nullptr), "write similarity");
            Check(worksheet_write_string(ws, r + 1, base + 3, StatusText(item.status), nullptr), "write status");
        }
        worksheet_set_column(ws, base + 0, base + 1, 42.0, wrap);
        worksheet_set_column(ws, base + 2, base + 2, 12.0, nullptr);
        worksheet_set_column(ws, base + 3, base + 3, 14.0, nullptr);
    }

    worksheet_freeze_panes(ws, 1, 0);
    worksheet_autofilter(ws, 0, 0, static_cast<lxw_row_t>(max_rows), static_cast<lxw_col_t>(groups.size() * 4 - 1));

    WriteTable(guard->wb,"参数",parameters);
    WriteTable(guard->wb,"统计与来源",statistics);
    guard->Close();
#else
    (void)groups; (void)output;
    throw std::runtime_error("当前构建未启用 libxlsxwriter");
#endif
}

} // namespace adayo
