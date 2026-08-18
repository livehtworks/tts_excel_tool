# P3 Corpus UI Audit

Date: 2026-08-18

## Scope

- Added service-level corpus session management.
- Added wxWidgets mapping and runtime panels.
- Connected the workbook reader/config store to the corpus UI without exposing OpenXLSX document objects to UI state.

## Implemented Chain

`CorpusMappingPanel`

`WorkbookService`

`OpenXlsxWorkbookReader`

`ColumnAnalyzer`

`CorpusViewService`

`ViewBuilder`

`CorpusRunPanel`

## Acceptance Checks

- Runtime view keeps reference columns before play columns.
- Each play column gets a following result column.
- Multiline play cells expand through `ViewBuilder`.
- Editing one expanded play segment updates only the matching source row, source column, and segment index.
- Result cells cycle `blank -> OK -> NG -> blank` in runtime session data.
- Result marks are not written back into source worksheet rows.
- Mapping persistence is keyed by workbook identity, sheet, and header row.

## Commands

```cmd
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
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

## Manual Notes

- `TtsPanel` is now a corpus workspace with `列映射` and `运行视图` tabs.
- `CorpusMappingPanel` saves config to `config/config.json` beside the executable.
- `CorpusRunPanel` allows grid edits and result cycling by double-clicking result cells.

## Not Covered Here

- Audio playback buttons are owned by P5.
- Excel export is owned by P7.
