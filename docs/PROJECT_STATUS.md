# Project Status

Current phase: source publication and GPT research handoff after downloaded voice configuration. Piper inventory is 174 variants / 50 languages / 55 locales; 171 variants and all 2703 associated speaker slots pass native synthesis. Three variants need additional native frontend capability, and MOSS native inference remains unimplemented. Resource details and warnings are in `docs/VOICE_RESOURCES.md` and `docs/VOICE_RESOURCE_INVENTORY.json`. Prior full Release 7/7 (including fresh Unicode initialization and 500 EN + 500 ZH generations), Core 4/4 and eight preparation contracts pass. The 170 prepared weights pass original-prefix/prepared SHA256 and hard-link checks. The user now authorizes source commit/push including the voice-configuration work and `docs/GPT_RESEARCH_HANDOFF_20260906.md`; the proposed missing capabilities are not implemented by this handoff. The earlier work-package acceptance remains 29 PASS / 7 NOT_RUN, not an overall acceptance PASS; its evidence is indexed in `docs/REVIEW_EXECUTION_20260906.md`.

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
- Native resource registry now has 172 model configurations / 2704 selectable speaker entries, including the existing Xiao Ya INT8 model. Arabic's two and Spanish's nine downloaded variants are configured and have real synthesis evidence. Six older variants retain explicit original-phoneme-table warnings; do not equate PCM smoke with listening acceptance.
- Hebrew saspeech, Japanese hi_fi_captain and English Mike remain unregistered because their required frontends/features are unavailable. MOSS's 22 files / 8 ONNX graphs and external weights are complete, but its native inference remains explicitly blocked.
- Prepared weights share physical files with downloaded resources through hard links; metadata journals preserve original-prefix hashes. Character-based Ukrainian uses explicit NFD input and all three speakers pass real synthesis. The canonical dependency root remains unchanged.
- September review outputs are create-only `dist/review-<HEAD>-<run_id>/AdayoCorpusTool`. Existing runtime outputs are protected. ZIP creation requires a separate explicit request.
- Source commits exclude build/dist/models payloads, private workbooks, recordings, local configs/logs and the archived old project.
- Old project remains read-only under `backup/old_project_20260818_160848`. Earlier status history is archived in `docs/history/PROJECT_STATUS_PRE_SEPTEMBER_ACCEPTANCE.md`.

## Verification And Remaining Acceptance

- Current Release 7/7 passes, including 500 EN + 500 ZH real generations, model switching and Unicode-path initialization before any ASCII espeak load. Core 4/4 passes with explicit reduced Unicode capability. Resource preparation contracts pass 8/8; registry coverage is 171 downloaded models / 2703 speaker slots, plus the existing Xiao Ya INT8 entry.
- Handoff pre-commit recheck: targeted Release 6/6 (excluding the already verified long-running P4 synthesis suite), Core 4/4 and preparation 8/8. Logs: `logs/research-handoff-{release,core,preparation}-tests.log`. This does not rerun the full speaker inventory or establish pronunciation parity. The MOSS blocked test verifies rejection, not working synthesis.
- Previous EN/ZH acceptance covered 20 baseline, 20 memory and 20 separate-process disk requests per language. Hits made zero engine calls and retained identical PCM. Another 120 real WASAPI loopback recordings passed head/tail correlation. This is not playback/listening evidence for all newly configured voices.
- The previous isolated review EXE ran from an external working directory: original workbook, mapping, playback/cache, manual result, compare and both exports. Native and OOXML readback passed. Status overlap, stale idle status and clipped metric headers discovered during GUI acceptance were fixed and rechecked.
- Pending user verification: human listening; the full cold/warm playback/control/close and live clear matrix; rapid file/sheet/header changes; multi-language edit/range/cursor combinations; close/cancel/reopen during background work. Automated portions of these composite cases are documented separately, not promoted to full PASS.
- Arabic/Spanish resource blockers are resolved by preparing the already downloaded weights; no voice substitution or bulk redownload was performed. New language listening/business acceptance and actual ASR accuracy remain NOT_RUN where recognizer output or human assessment has not been supplied.
- Evidence limitation: original workbook preflight hash and isolated review/model/junction protections passed, but no full pre-task hash inventory of all established production/backup files was recorded. This cannot be reconstructed retrospectively.
- The earlier work-package source commit/push is complete. The current handoff explicitly requests another source commit/push. The desktop executable is rebuilt; EXE-only replacement of the established runtime still awaits explicit user approval. No runtime replacement, release ZIP or packaging is part of this handoff; no user-owned running review process was stopped.
