# P2 Workbook Reader and Config Store Audit

Date: 2026-08-18

## Scope

- Enabled real `.xlsx` reading through OpenXLSX 0.5.1.
- Added JSON config persistence through nlohmann-json 3.12.0.
- Verified the P0 desensitized legacy business fixture against the C++ workbook pipeline.

## Implemented Chain

`tests/fixtures/adayo_legacy_business_fixture.xlsx`

`OpenXlsxWorkbookReader`

`ColumnAnalyzer`

`ViewBuilder`

`JsonConfigStore`

## Acceptance Checks

- `SheetNames()` returns `Vehicle`, `System`, `EmptySheet`.
- `ReadSheet(..., "Vehicle", 2)` returns 10 headers and 4 non-empty data rows.
- Column analysis maps:
  - `ENG` -> `en-GB`
  - `ENU` -> `en-US`
  - `FRF` -> `fr-FR`
  - `ARG` -> `ar-SA`
  - `SPM` -> `es-ES`
  - `ENG结果` -> result column with `en-GB`
- Runtime view expands multiline play cells to 8 display rows for the selected `三级功能`, `ENG`, and `ENU` scenario.
- JSON config store saves and loads workbook identity, sheet name, thresholds, header row, and column profiles.
- Workbook reader can read the same fixture after copying it to a Chinese directory and Chinese `.xlsx` filename.

## Commands

```cmd
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
cmake --fresh --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

Result:

```text
1/2 adayo_core_tests PASS
2/2 adayo_p2_tests PASS
100% tests passed
```

GUI smoke:

```text
AdayoCorpusTool smoke alive_after_5s=True
```

## Notes

- OpenXLSX accepts a narrow string path. For non-ASCII workbook paths, the adapter copies the input workbook to a temporary ASCII staging path, reads it, and removes the staging copy after close.
- Python remains absent from the runtime architecture. Prior Python usage was limited to development-time fixture/tool setup.
