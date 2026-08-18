# R4 Compare, Text Import, and UI Responsiveness

Date: 2026-08-18

## Scope

- Added multi-language compare groups and four-column-per-group Excel export.
- Replaced UI-local TXT parsing with `TextFileImporter`, including UTF-8 BOM and fixed delimiter support.
- Switched release punctuation handling to utf8proc Unicode punctuation categories.
- Moved long-running Compare, Compare export, runtime export, workbook sheet loading, and workbook analysis work to `WorkerQueue`.

## Verification

- `cmake --build --preset windows-release --config Release`
- `ctest --preset windows-release -R "adayo_p6_compare_tests|adayo_p7_export_tests|adayo_p2_tests" --output-on-failure`
  - 3/3 passed
- `cmake --build --preset windows-core --config Debug`
- `ctest --preset windows-core --output-on-failure`
  - 4/4 passed

## Notes

- Core builds without utf8proc keep the documented limited punctuation fallback; the full Unicode category assertion runs only when `ADAYO_HAS_UTF8PROC` is enabled.
- `ExportComparison()` remains as a single-group compatibility wrapper around `ExportComparisonGroups()`.
