# P7 Excel Export Audit

Date: 2026-08-18

## Scope

- Enabled `LibXlsxWriterExporter` in the release preset.
- Connected runtime view export and compare report export from wx UI.
- Verified Chinese output path support through exporter-side ASCII staging.

## Acceptance

- Runtime view exports current display rows and manual result symbols.
- Compare report exports reference text, actual text, similarity, and status.
- Changed diff fragments are written through libxlsxwriter rich strings with red font.
- Freeze panes, autofilter, text wrapping, and fixed column widths are applied.
- Chinese, English, and Arabic content are read back correctly.
- Chinese output directory and Chinese output filename are verified.

## Commands

```cmd
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
cmake --fresh --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release -R adayo_p7_export_tests --output-on-failure
```

Result:

```text
adayo_p7_export_tests PASS
```

## Local vcpkg Note

The tested vcpkg `libxlsxwriter` install omitted third-party headers expected by `xlsxwriter/common.h`. The missing headers were copied from the same package source tree into the local vcpkg installed include directory:

`build/windows-release/vcpkg_installed/x64-windows/include/third_party`

This is recorded in `docs/EXECUTION_NOTES.md`.
