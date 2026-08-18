# Project Status

Current phase: P2 workbook reader and config store verified.

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
- `hf-mirror.com` works for small sherpa/Piper model metadata downloads without proxy.
- Downloaded local candidate sherpa/Piper models:
  - `models/sherpa/vits-piper-en_US-amy-low`
  - `models/sherpa/vits-piper-zh_CN-huayan-x_low`

## Active Work

- Move next to P3 UI wiring so workbook open/header selection/mapping persistence use the verified C++ services.
- Keep sherpa-onnx as a separate pinned native dependency before enabling real TTS.

## Not Yet Complete

- UI is not yet wired to the OpenXLSX reader/config store.
- libxlsxwriter exporter is not fully wired to a pinned package.
- sherpa-onnx native TTS is not verified.
- Local TTS model files exist, but model registry schema and sherpa native runtime are not wired yet.
- miniaudio playback is still a placeholder.
- No production model package has been downloaded or accepted yet.
