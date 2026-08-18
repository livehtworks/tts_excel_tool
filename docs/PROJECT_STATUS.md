# Project Status

Current phase: P1 Windows build and dependency baseline.

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
- `hf-mirror.com` works for small sherpa/Piper model metadata downloads without proxy.
- Downloaded local candidate sherpa/Piper models:
  - `models/sherpa/vits-piper-en_US-amy-low`
  - `models/sherpa/vits-piper-zh_CN-huayan-x_low`

## Active Work

- Establish pinned Windows dependency baseline with vcpkg.
- Continue to P2 workbook reader/config store work once P1 is committed.
- Keep sherpa-onnx as a separate pinned native dependency before enabling real TTS.

## Not Yet Complete

- OpenXLSX reader is not implemented.
- libxlsxwriter exporter is not fully wired to a pinned package.
- sherpa-onnx native TTS is not verified.
- Local TTS model files exist, but model registry schema and sherpa native runtime are not wired yet.
- miniaudio playback is still a placeholder.
- No production model package has been downloaded or accepted yet.
