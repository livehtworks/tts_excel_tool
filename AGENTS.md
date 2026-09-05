# Project Profile

- This is a Windows C++20 wxWidgets desktop corpus playback and text comparison tool. The directory name does not imply a Python application. Python is not a production/runtime dependency.
- Read `docs/PROJECT_STATUS.md` and `docs/EXECUTION_NOTES.md` before work. Architecture is in `docs/ARCHITECTURE.md`; persistent file authority is in `docs/DATA_FACTS.md`. Historical acceptance reports are not current gates.
- `ApplicationRuntime` owns configuration, logging, services and worker shutdown. `PlaybackService` owns request cancellation/pause state; `TtsService` owns synthesis and the bounded audio cache. `CorpusViewService` owns session edits/results/source identities. `CompareService` owns option/metric contracts.
- Original workbooks are read directly and are never rewritten by import. There is no database. Runtime paths derive from the executable directory.
- Canonical resource source is `dist/AdayoCorpusTool/model`. Model data, existing runtime outputs and `backup/` are protected and not source-control payloads.
- Builds use the existing `windows-core` and `windows-release` presets, with MSVC initialized as documented in execution notes. Test commands are `ctest --test-dir build/windows-core --output-on-failure` and the equivalent `build/windows-release` command. Full desktop capability requires all Release dependency switches ON.
- Current source publication branch is `main`, remote `origin`. Local machine overrides are only in ignored `CMakeUserPresets.json`. Build/dist/private fixtures/config/logs/recordings remain outside commits.
- Packaging is create-only by default under a unique review directory. Do not create a release ZIP or replace an established runtime without explicit user authorization.
