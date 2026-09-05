# Project Status

Current phase: September 6 review implementation A-F committed; G implementation and final acceptance in progress. The audit baseline is `9e93d1b`. Acceptance evidence is tracked in `docs/REVIEW_EXECUTION_20260906.md`; older R0-R8 logs are historical, not the current acceptance authority.

## Architecture And Ownership

- Windows C++20, wxWidgets, CMake/Ninja/MSVC; no Python runtime or browser UI.
- `ApplicationRuntime` owns config/logging, workbook/model services, TTS/audio and two application workers: BackgroundJobWorker and PlaybackWorker.
- Workbook import uses OpenXLSX directly on the original file. Source path/hash/sheet/header, physical rows, merges and reference owners travel through the session and export. Workbook data is not rewritten.
- Mapping input revisions gate analysis/build/save. Runtime edits retain source identities and invalidate exactly the affected result marks.
- Playback requests own cancellation and pause state through the device handoff. Repeated Stop is idempotent; old completions cannot overwrite a newer request.
- TtsService validates full model/config/asset identity before its bounded memory/disk audio cache. Cache hits bypass engine load/synthesis. Epoch clear cannot refill from old generation work.
- CompareService shares four versioned option snapshots: legacy Indel sequence matching, strict raw row equality, CER and WER. Full options, provenance and weighted statistics reach the virtual grid and XLSX export.
- Comparison uses one 512MiB allocation budget, request-specific cancellation, strict input decoding, 64MiB per input and 65536 code points per record.
- Atomic config/export writes check write, flush, close and replacement; XLSX limits and rich-text reconstruction are checked before publication.
- Persistent data ownership is documented in `docs/DATA_FACTS.md`.

## Resources And Delivery

- Source remote: `https://github.com/livehtworks/tts_excel_tool.git`, branch `main`.
- Canonical resource source: `dist/AdayoCorpusTool/model`. Do not change or clear it during acceptance.
- Native accepted model IDs: `vits-piper-en_US-amy-low` and `vits-piper-zh_CN-xiao_ya-medium-int8`.
- Raw downloaded Piper and MOSS assets are not automatically native-usable voices. Arabic/Spanish native acceptance needs complete converted resources. MOSS native inference remains explicitly blocked.
- September review outputs are create-only `dist/review-<HEAD>-<run_id>/AdayoCorpusTool`. Existing runtime outputs are protected. ZIP creation requires a separate explicit request.
- Source commits exclude build/dist/models payloads, private workbooks, recordings, local configs/logs and the archived old project.
- Old project remains read-only under `backup/old_project_20260818_160848`. Earlier status history is archived in `docs/history/PROJECT_STATUS_PRE_SEPTEMBER_ACCEPTANCE.md`.

## Remaining Acceptance

- Complete final Release/Core regression and real voice stress.
- Complete isolated review-program GUI and loopback scenarios and evidence aggregation.
- Human listening and target-user business acceptance remain separate gates; do not infer them from automated tests.
- Final staged commit/push is explicitly authorized by the current user request.
