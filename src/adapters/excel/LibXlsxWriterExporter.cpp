#include "adapters/excel/LibXlsxWriterExporter.h"

#include "platform/FileIo.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

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
            Check(worksheet_write_string(ws, 0, base + c, (prefix + headers[c]).c_str(), header), "write compare header");
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
            Check(worksheet_write_number(ws, r + 1, base + 2, item.similarity, nullptr), "write similarity");
            Check(worksheet_write_string(ws, r + 1, base + 3, StatusText(item.status), nullptr), "write status");
        }
        worksheet_set_column(ws, base + 0, base + 1, 42.0, wrap);
        worksheet_set_column(ws, base + 2, base + 2, 12.0, nullptr);
        worksheet_set_column(ws, base + 3, base + 3, 14.0, nullptr);
    }

    worksheet_freeze_panes(ws, 1, 0);
    worksheet_autofilter(ws, 0, 0, static_cast<lxw_row_t>(max_rows), static_cast<lxw_col_t>(groups.size() * 4 - 1));

    guard->Close();
#else
    (void)groups; (void)output;
    throw std::runtime_error("当前构建未启用 libxlsxwriter");
#endif
}

} // namespace adayo
