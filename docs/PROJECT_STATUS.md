# Project Status

Current phase: executing the 2026-09-06 review work package against baseline `9e93d1b`, in stages A-G. Earlier acceptance is not final acceptance of this work. Real workbook fixtures and detailed local evidence remain outside the public repository.

## Current Architecture

- C++20 / CMake project.
- wxWidgets is the only intended desktop UI stack.
- Core logic is separated from adapters and UI.
- `ApplicationRuntime` owns process-lifetime services, logging, model registry, TTS/audio/playback resources, and the single background job worker.
- Old Python project is archived under `backup/old_project_20260818_160848` and is not a runtime dependency.

## Completed Facts

- Old project was moved into `backup/old_project_20260818_160848`.
- Git repository was initialized in the workspace root.
- Source repository: `https://github.com/livehtworks/tts_excel_tool.git`; the publication branch is `main`. Source, tests, scripts, and project documentation are versioned; runtime models, build/release outputs, local configuration, and the archived old project remain local.
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
- `ModelRegistry` scans release/runtime model metadata from the executable sibling path `model/sherpa/*/model.json`.
- English and Chinese sherpa/Piper voices generated through native API.
- `PlaybackService` implements the playback state machine on its dedicated `PlaybackWorker`.
- `MiniaudioPlayer` consumes float PCM `AudioBuffer` directly.
- `ComparePanel` imports text files, runs `CompareService`, and displays alignment results.
- `LibXlsxWriterExporter` is enabled and exports runtime/compare workbooks.
- Runtime and compare Excel export are verified in a Chinese output path.
- `MossNanoTtsEngine` is compiled as an explicit-fail adapter and covered by `adayo_p8_moss_blocked_tests`.
- P8 official MOSS ONNX sources were reviewed; status is `MOSS_PORT_BLOCKED` until native tokenizer and golden parity are implemented.
- `scripts/package_windows.ps1` updates the fixed release directory `dist/AdayoCorpusTool` in place, validates `dist/AdayoCorpusTool/model`, and does not create a ZIP unless explicitly requested.
- Previous P9 local release regression and package smoke evidence is superseded by R0 because Release tests used `assert()` before R0.
- R0 replaced Release-unsafe `assert()` tests with always-on test checks and exposed the R1-1 `ENG结果` column classification failure.
- R1 fixed result-column recognition, default mapping roles, `tts_model_id` persistence, schema v2 language override semantics, path-only workbook identity, sheet/header row storage, and model registry diagnostics.
- R2 fixed runtime result identity, synthetic blank editing/result rejection, and avoided full rebuilds for ordinary result cycles and non-structural edits.
- R3 wired the runtime UI to the real playback chain: selected play column, row range, interval, speed, pause/resume/stop, model-id playback, and current-cell highlighting.
- R3 fixed pause during generation, interruptible intervals, stale worker stop/new-sequence state ownership, and miniaudio same-format device reuse.
- R3 re-ran sherpa acceptance under always-on checks: speed sample-count ordering, 20 EN/ZH model switches, 500 English generations, and 500 Chinese generations.
- R4 replaced Compare UI's single-pair flow with multi-language input groups and a matching report grid/export format where each group owns four columns.
- R4 moved UTF-8 BOM/fixed-delimiter text import into `TextFileImporter` and tests it directly.
- R4 text import now also supports UTF-16LE/BE BOM input and converts records to UTF-8 before comparison.
- R4 uses utf8proc Unicode punctuation categories in release builds, including Arabic punctuation coverage.
- R4 moved Compare work, Compare export, runtime Excel export, and workbook sheet/analyze operations off the wx UI thread.
- R5 made JSON config load/save safer: corrupt files are renamed to `.corrupt-*`, future schema files are not overwritten, and normal saves use same-directory temp files plus atomic replacement.
- R5 removed personal absolute dependency roots from shared CMake presets. `windows-release` now reads `VCPKG_ROOT` and `ADAYO_SHERPA_ONNX_ROOT`.
- R5 makes desktop application dependencies mandatory when `ADAYO_BUILD_DESKTOP=ON`.
- R5 added a vcpkg overlay port for `libxlsxwriter@1.2.4#2` so third-party headers are installed reproducibly.
- R5 added `models/package-manifest.json`; packaging copies and validates only listed local sherpa model ids.
- R6 local automated regression passed on this workstation: `windows-core` 4/4 and `windows-release` 7/7.
- R6 performance evidence recorded: P6 1000x992 in 258 ms, P6 5000x4990 in 6514 ms, English sherpa 500 avg 64.068 ms p95 79 ms, Chinese sherpa 500 avg 400.938 ms p95 456 ms.
- R6 release package generated under `dist/AdayoCorpusTool-win-x64-R6-9157b64.zip`; SHA-256 is `C94867626890DC9E6F2D5D1CA6E6BC33BD5BA8CC11A9BBE5C65AC80DB81DE792`.
- Model/voice runtime resources are centralized under `dist/AdayoCorpusTool/model`.
- ModelScope works as the domestic no-proxy source for Piper and MOSS model resources.
- Downloaded and validated Piper full voice assets under `dist/AdayoCorpusTool/model/piper/voices`: 348 files, 174 voices, 50 language directories, 10.688 GiB / 11.476 GB.
- Downloaded and validated MOSS ONNX resources under `dist/AdayoCorpusTool/model/moss`: `MOSS-TTS-Nano-100M-ONNX` and `MOSS-Audio-Tokenizer-Nano-ONNX`, 22 files, 0.710 GiB / 0.763 GB combined.
- Existing sherpa runtime models were moved under `dist/AdayoCorpusTool/model/sherpa`, including accepted English/Chinese models and the local huayan candidate.
- R7 fixed wx-facing Chinese text by routing UI strings through explicit UTF-8 conversion.
- R7 centralized Windows path conversion through `PathToUtf8` / `PathFromUtf8` and removed business-layer `path.string()` usage.
- R7 removed OpenXLSX `%TEMP%` staging and reads requested Unicode workbook paths directly.
- R7 changed libxlsxwriter export to memory output buffer + native filesystem write, with no temporary ASCII output path.
- R7 added `ApplicationRuntime` and fixed the project-owned worker model to two threads: `BackgroundJobWorker` and `PlaybackWorker`; UI panels no longer own `WorkerQueue`.
- R7 moved TTS model ensure/load/switch into `PlaybackService`'s worker path. UI no longer calls `TtsService::LoadModel()` or `Unload()`.
- R7 restored the frozen interaction: single-clicking a Play cell starts that sentence, and single-clicking a Result cell cycles blank/OK/NG.
- R7 local acceptance is superseded by the R8 audit because the R8 source audit reproduced an invalid Reference-edit result invalidation scope in the old R7 semantics.
- R7 added UTF-8 `FileLogger` under `<exe dir>/logs`.
- R7 sherpa Unicode model path gate passed on this workstation with real copied EN/ZH models under a multi-level Chinese path. Evidence: `logs/r7-p4-tts-unicode-path-gate.log`.
- R7 local automated regression evidence is retained for history only and is no longer an acceptance authority for current work.
- R8 local automatic remediation build/test passed on this workstation: `windows-core` 4/4 and `windows-release` 7/7. Evidence: `logs/r8-windows-core-ctest-2.log`, `logs/r8-windows-release-ctest-2.log`.
- R8 config authority is centralized in `ApplicationRuntime`; Mapping/Run/Compare panels use runtime config snapshots and controlled update/save calls.
- R8 config schema is v3 with explicit Auto/Fixed language selection mode, so a user-fixed locale remains fixed even when it equals the analyzer guess.
- R8 Compare UI uses `CompareGridTable` over an immutable report snapshot, and export workers share that snapshot instead of deep-copying the report on the UI thread.
- R8 packaging script builds from fresh staging, copies allowlisted runtime files, validates package-contained models from `-ModelSourceRoot`, discovers VC runtime through environment/vswhere, and runs dumpbin dependency closure validation.
- R8 source handoff script uses a git allowlist, excludes build/dist/vcpkg/user artifacts, and scans source payload text for personal absolute paths.
- Historical `CODEX_EXECUTION_PLAN.md` was marked `SUPERSEDED_DO_NOT_EXECUTE` and moved under `docs/history/`.

## Active Work

- R8 remediation is active against `AdayoCorpusTool_R8_FULL_SOURCE_AUDIT_CLOSED_LOOP_REMEDIATION_20260819.md`.
- P9 target-machine acceptance remains revoked.
- Final R8 target-machine U01-U08 acceptance has not been executed.

## Not Yet Complete

- R8 final closed-loop acceptance is not yet complete.
- Full P9 target-machine acceptance is not yet complete.
- MOSS-TTS-Nano native adapter is intentionally blocked from runtime enablement.
