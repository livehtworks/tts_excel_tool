# Project Status

Current phase: R2 safety/interaction/cache source handoff with incomplete composite acceptance. Baseline `5b7bee0` matched local/remote preflight with a clean worktree. Current execution and item statuses are in `docs/R2_SAFETY_UI_CACHE_REVIEW_20260906.md`; raw evidence and independent builds are outside the repository. No R2 overall PASS is claimed. The latest user request authorizes source commit/push; canonical model writes, production EXE replacement and ZIP remain excluded. The prior voice inventory remains 174 Piper variants / 50 languages / 55 locales, with historical synthesis checks for 171 variants and 2703 speaker slots. Three frontends and MOSS native inference remain unimplemented, and six variants retain historical phoneme warnings. The earlier work-package acceptance remains 29 PASS / 7 NOT_RUN, not an overall acceptance PASS; its evidence is indexed in `docs/REVIEW_EXECUTION_20260906.md`.

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

- F5 full Release passes 7/7 in 711.48 seconds, including the real native
  cancellation/identity contracts and 1000 EN/ZH generations. F7 Core passes
  4/4 in 4.87 seconds. Configuration restart coverage now repeats three times.
  Audio diagnostics retain logical request and shared load identity separately;
  frozen old-v1 cache keys/PCM and zero-engine-call hits pass for all four voices.
  The bounded shared digest lookup now uses one hash-table lookup, with the
  complete resource enumeration/attribute validation unchanged. F4's three-round
  hit recheck has no aggregate p95 increase over 10%; F5 has an Amy disk outlier
  concentrated in its first round (183.19/149.65/133.96 ms versus baseline
  149.70/138.55/144.25 ms). It is retained for investigation, not a performance
  PASS. Detailed evidence is in the current R2 review document.

- F1/F3 continuation: native window-destruction timestamps now exist. Idle and
  real paused-playback close exit without a process remaining; the user confirms
  the 47-row English sequence sounds normal. Full-registry GUI selects Amy and
  displays LibriTTS speakers; this is not complete selection-set/DPI coverage.
  A new real Sherpa contract test exposed synthesis continuing after cancellation
  at model-load return. The missing boundary check is fixed and the same case
  passes, together with SID separation, thread/rule/model reload, NFD and speed
  clamping. Real Windows config file locking, field rollback, unlocked retry and
  full Runtime restart pass. F3 targeted Release: 6/6 in 7.94 seconds, plus the
  dedicated native contract case. F1 performance retains an Amy memory p95
  regression; analysis and composite GUI/lifecycle acceptance remain active.

- R2 current checkpoint: reported mapping-selection crash and advanced-pane
  zero-height grid are fixed. E1 GUI additionally verifies speaker-name search,
  multiline editing, dirty-leave cancellation and rejection/recovery of duplicate
  tokens. E3 full Release 7/7 (including 1000 native generations), E3 Core 4/4,
  and preparation 15/15 pass. E4 also measures 50 alternating-speaker real
  syntheses with one initial load. E5 rebuilds the subsequent mapping-file-error
  and shutdown-start ordering fixes; its targeted regression is indexed in the
  review document. Partial-clear and abnormal-exit cache tests use real files.
  Scoped protected-file comparison passes 758 records. These are partial R2
  results, not complete interaction/device/DPI/lifecycle acceptance. Evidence
  and remaining assertions are indexed by the current R2 review document.
- Pre-R2 handoff Release 7/7 passed, including 500 EN + 500 ZH real generations, model switching and Unicode-path initialization before any ASCII espeak load. That handoff's Core 4/4 and preparation 8/8 are historical results, not current R2 totals. Its registry coverage was 171 downloaded models / 2703 speaker slots, plus the existing Xiao Ya INT8 entry.
- Handoff pre-commit recheck: targeted Release 6/6 (excluding the already verified long-running P4 synthesis suite), Core 4/4 and preparation 8/8. Logs: `logs/research-handoff-{release,core,preparation}-tests.log`. This does not rerun the full speaker inventory or establish pronunciation parity. The MOSS blocked test verifies rejection, not working synthesis.
- Previous EN/ZH acceptance covered 20 baseline, 20 memory and 20 separate-process disk requests per language. Hits made zero engine calls and retained identical PCM. Another 120 real WASAPI loopback recordings passed head/tail correlation. This is not playback/listening evidence for all newly configured voices.
- The previous isolated review EXE ran from an external working directory: original workbook, mapping, playback/cache, manual result, compare and both exports. Native and OOXML readback passed. Status overlap, stale idle status and clipped metric headers discovered during GUI acceptance were fixed and rechecked.
- Pending user verification: human listening; the full cold/warm playback/control/close and live clear matrix; rapid file/sheet/header changes; multi-language edit/range/cursor combinations; close/cancel/reopen during background work. Automated portions of these composite cases are documented separately, not promoted to full PASS.
- Arabic/Spanish resource blockers are resolved by preparing the already downloaded weights; no voice substitution or bulk redownload was performed. New language listening/business acceptance and actual ASR accuracy remain NOT_RUN where recognizer output or human assessment has not been supplied.
- Evidence limitation: original workbook preflight hash and isolated review/model/junction protections passed, but no full pre-task hash inventory of all established production/backup files was recorded. This cannot be reconstructed retrospectively.
- The previous source handoff was committed/pushed as `5b7bee0`. The latest request authorizes the R2 source commit/push, but not established runtime replacement. Baseline/final builds and application directories are isolated outside the repository. No user-owned running review process is stopped. E1 automation was interrupted without discarding its session; it was no longer present when this continuation resumed. This continuation's F1/F5/F6 comparison windows closed normally after their exports.
