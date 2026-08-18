#include "adapters/excel/LibXlsxWriterExporter.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef ADAYO_HAS_LIBXLSXWRITER
#include <xlsxwriter.h>
#endif

namespace adayo {
#ifdef ADAYO_HAS_LIBXLSXWRITER
namespace {
std::string PathUtf8(const std::filesystem::path& p) {
#if defined(__cpp_lib_char8_t)
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
#else
    return p.u8string();
#endif
}

bool ContainsNonAscii(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::any_of(text.begin(), text.end(), [](char8_t c) {
        return static_cast<unsigned char>(c) >= 0x80;
    });
}

std::filesystem::path MakeAsciiOutputPath(const std::filesystem::path& original) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto hash = std::hash<std::string>{}(PathUtf8(original));
    auto dir = std::filesystem::temp_directory_path() / "adayo_xlsxwriter";
    std::filesystem::create_directories(dir);
    return dir / ("export_" + std::to_string(hash) + "_" + std::to_string(ticks) + ".xlsx");
}

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
    std::filesystem::path working_path;
    bool closed{false};

    WorkbookGuard() = default;
    WorkbookGuard(lxw_workbook* workbook, std::filesystem::path final, std::filesystem::path working)
        : wb(workbook), final_path(std::move(final)), working_path(std::move(working)) {}
    WorkbookGuard(const WorkbookGuard&) = delete;
    WorkbookGuard& operator=(const WorkbookGuard&) = delete;
    WorkbookGuard(WorkbookGuard&& other) noexcept
        : wb(other.wb),
          final_path(std::move(other.final_path)),
          working_path(std::move(other.working_path)),
          closed(other.closed) {
        other.wb = nullptr;
        other.closed = true;
    }
    WorkbookGuard& operator=(WorkbookGuard&& other) noexcept {
        if (this == &other) return *this;
        wb = other.wb;
        final_path = std::move(other.final_path);
        working_path = std::move(other.working_path);
        closed = other.closed;
        other.wb = nullptr;
        other.closed = true;
        return *this;
    }

    ~WorkbookGuard() {
        if (wb && !closed) workbook_close(wb);
        if (!working_path.empty() && working_path != final_path) {
            std::error_code ec;
            std::filesystem::remove(working_path, ec);
        }
    }

    void Close() {
        const lxw_error close_error = workbook_close(wb);
        closed = true;
        Check(close_error, "workbook_close");
        if (!working_path.empty() && working_path != final_path) {
            if (final_path.has_parent_path()) {
                std::filesystem::create_directories(final_path.parent_path());
            }
            std::filesystem::copy_file(working_path, final_path, std::filesystem::copy_options::overwrite_existing);
        }
    }
};

WorkbookGuard CreateWorkbook(const std::filesystem::path& output) {
    const auto working = ContainsNonAscii(output) ? MakeAsciiOutputPath(output) : output;
    if (working.has_parent_path()) {
        std::filesystem::create_directories(working.parent_path());
    }
    WorkbookGuard guard{workbook_new(PathUtf8(working).c_str()), output, working};
    if (!guard.wb) throw std::runtime_error("workbook_new 失败");
    return guard;
}
}
#endif

void LibXlsxWriterExporter::ExportRuntimeView(const RuntimeView& view, const std::filesystem::path& output) {
#ifdef ADAYO_HAS_LIBXLSXWRITER
    auto guard = CreateWorkbook(output);
    lxw_worksheet* ws = workbook_add_worksheet(guard.wb, "运行视图");
    if (!ws) throw std::runtime_error("workbook_add_worksheet 失败");

    lxw_format* header = workbook_add_format(guard.wb);
    format_set_bold(header);
    format_set_text_wrap(header);
    lxw_format* wrap = workbook_add_format(guard.wb);
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

    guard.Close();
#else
    (void)view; (void)output;
    throw std::runtime_error("当前构建未启用 libxlsxwriter");
#endif
}

void LibXlsxWriterExporter::ExportComparison(const std::vector<CompareRow>& rows, const std::filesystem::path& output) {
#ifdef ADAYO_HAS_LIBXLSXWRITER
    auto guard = CreateWorkbook(output);
    lxw_worksheet* ws = workbook_add_worksheet(guard.wb, "文本对比");
    if (!ws) throw std::runtime_error("workbook_add_worksheet 失败");

    lxw_format* header = workbook_add_format(guard.wb);
    format_set_bold(header);
    format_set_text_wrap(header);
    lxw_format* wrap = workbook_add_format(guard.wb);
    format_set_text_wrap(wrap);
    lxw_format* changed = workbook_add_format(guard.wb);
    format_set_font_color(changed, LXW_COLOR_RED);

    const char* headers[] = {"正式文本", "机器文本", "相似度", "结果"};
    for (lxw_col_t c = 0; c < 4; ++c) Check(worksheet_write_string(ws, 0, c, headers[c], header), "write compare header");

    for (lxw_row_t r = 0; r < rows.size(); ++r) {
        const auto& item = rows[r];
        if (item.reference_index && item.actual_index) {
            WriteRichFragments(ws, r + 1, 0, item.diff.reference_fragments, changed, wrap);
            WriteRichFragments(ws, r + 1, 1, item.diff.actual_fragments, changed, wrap);
        } else {
            Check(worksheet_write_string(ws, r + 1, 0, item.reference_text.c_str(), wrap), "write reference");
            Check(worksheet_write_string(ws, r + 1, 1, item.actual_text.c_str(), wrap), "write actual");
        }
        Check(worksheet_write_number(ws, r + 1, 2, item.similarity, nullptr), "write similarity");
        Check(worksheet_write_string(ws, r + 1, 3, StatusText(item.status), nullptr), "write status");
    }

    worksheet_freeze_panes(ws, 1, 0);
    worksheet_autofilter(ws, 0, 0, static_cast<lxw_row_t>(rows.size()), 3);
    worksheet_set_column(ws, 0, 1, 42.0, wrap);
    worksheet_set_column(ws, 2, 2, 12.0, nullptr);
    worksheet_set_column(ws, 3, 3, 14.0, nullptr);

    guard.Close();
#else
    (void)rows; (void)output;
    throw std::runtime_error("当前构建未启用 libxlsxwriter");
#endif
}

} // namespace adayo
