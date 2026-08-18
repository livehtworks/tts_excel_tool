# Project Status

Current phase: P9 acceptance revoked pending remediation R0-R6.

## Current Architecture

- C++20 / CMake project.
- wxWidgets is the only intended desktop UI stack.
- Core logic is separated from adapters and UI.
- Old Python project is archived under `backup/old_project_20260818_160848` and is not a runtime dependency.

## Completed Facts

- Old project was moved into `backup/old_project_20260818_160848`.
- Git repository was initialized in the workspace root.
- Rewrite seed was expanded into the workspace root.
- P0 desensitized business fixture was added under `tests/fixtures`.
- Core build on Windows MSVC requires explicit `/utf-8`; this is now set in `CMakeLists.txt`.
- Core tests pass with MSVC 19.44.35225.0 via `build-core`.
- `windows-core` CMake preset builds and tests successfully.
- vcpkg manifest dry-run resolves dependencies with wxWidgets locked to 3.2.8.1.
- vcpkg dependency install completed after user allowed proxy use for C++ dependencies.
- `windows-release` preset builds `AdayoCorpusTool.exe` and passes core tests.
- Release EXE starts in both normal build directory and copied Chinese path directory.
- OpenXLSX workbook reader is enabled in `windows-release`.
- P2 tests read the desensitized legacy business fixture from both normal and Chinese filesystem paths.
- Column analysis now maps the legacy `ARG` header to Arabic `ar-SA` per the P0 business fixture.
- JSON config persistence is implemented through `JsonConfigStore`.
- `windows-release` now runs both `adayo_core_tests` and `adayo_p2_tests`.
- `WorkbookService` analyzes workbooks and restores mappings by workbook identity + sheet + header row.
- `CorpusViewService` owns runtime sessions, display edits, and result cycling.
- wxWidgets corpus UI now has separate mapping and runtime view panels.
- sherpa-onnx v1.13.6 native C API is linked in the release preset.
- `ModelRegistry` scans local `models/sherpa/*/model.json` metadata.
- English and Chinese sherpa/Piper voices generated through native API.
- `PlaybackService` implements the playback state machine on `WorkerQueue`.
- `MiniaudioPlayer` consumes float PCM `AudioBuffer` directly.
- `ComparePanel` imports text files, runs `CompareService`, and displays alignment results.
- `LibXlsxWriterExporter` is enabled and exports runtime/compare workbooks.
- Runtime and compare Excel export are verified in a Chinese output path.
- `MossNanoTtsEngine` is compiled as an explicit-fail adapter and covered by `adayo_p8_moss_blocked_tests`.
- P8 official MOSS ONNX sources were reviewed; status is `MOSS_PORT_BLOCKED` until native tokenizer and golden parity are implemented.
- `scripts/package_windows.ps1` packages the release app, fixed DLLs, runtime directories, accepted sherpa models, and audit docs without downloading.
- Previous P9 local release regression and package smoke evidence is superseded by R0 because Release tests used `assert()` before R0.
- R0 replaced Release-unsafe `assert()` tests with always-on test checks and exposed the R1-1 `ENG结果` column classification failure.
- R1 fixed result-column recognition, default mapping roles, `tts_model_id` persistence, schema v2 language override semantics, path-only workbook identity, sheet/header row storage, and model registry diagnostics.
- R2 fixed runtime result identity, synthetic blank editing/result rejection, and avoided full rebuilds for ordinary result cycles and non-structural edits.
- R3 wired the runtime UI to the real playback chain: selected play column, row range, interval, speed, pause/resume/stop, single-cell double-click playback, model-id loading, and current-cell highlighting.
- R3 fixed pause during generation, interruptible intervals, stale worker stop/new-sequence state ownership, and miniaudio same-format device reuse.
- R3 re-ran sherpa acceptance under always-on checks: speed sample-count ordering, 20 EN/ZH model switches, 500 English generations, and 500 Chinese generations.
- `hf-mirror.com` works for small sherpa/Piper model metadata downloads without proxy.
- Downloaded local candidate sherpa/Piper models:
  - `models/sherpa/vits-piper-en_US-amy-low`
  - `models/sherpa/vits-piper-zh_CN-huayan-x_low`

## Active Work

- Execute remediation checklist R0-R6 in order; R4 is next.

## Not Yet Complete

- R0-R6 remediation is not yet complete.
- Full P9 target-machine acceptance is not yet complete.
- MOSS-TTS-Nano native adapter is intentionally blocked from runtime enablement.
