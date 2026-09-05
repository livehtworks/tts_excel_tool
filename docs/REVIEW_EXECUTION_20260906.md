# September Review Execution

Baseline: `9e93d1b`. The user authorized this one unattended execution, staged commits and final source push. All 17 scoped implementation issues have been addressed. **Acceptance is not an overall PASS**: the local 36-case matrix contains 29 PASS and 7 NOT_RUN composite/evidence cases; the user explicitly permitted post-push manual verification.

## Issue Closure

| Work-package issue | Implementation | Evidence / commit |
| --- | --- | --- |
| PKG-01 | Create-only final/ZIP checks before staging; no existing runtime deletion | `scripts/package_windows.ps1`; A `92c9c71`, populated-target/ZIP sentinel test |
| PKG-02 | Literal tree copies with before/after/destination SHA, failed dumpbin rejection, validated System32 API-set and VC runtime closure | Package regression plus real isolated package; `f566c0f`, `7c66dd5`, `a5090dc` |
| PLAY-01 | Request-owned cancellation, idempotent Stop and stale-completion exclusion | `PlaybackService`; B `a07f211`, deterministic P5 barriers |
| PLAY-02 | Shared request context gates pause/cancel at the production audio handoff | `IAudioPlayer`, `MiniaudioPlayer`; P5 controls and separate real loopback evidence |
| CACHE-01 | TtsService-owned bounded persistent cache, complete identities, LRU/pin/epoch/lock safety, schema 4 and UI | `AudioCache`, `TtsService`, runtime/config/UI; C `399a8d6`, P2/P5 and real probes |
| XLSX-01 | Input revisions invalidate analysis/build/save; snapshot checks reject stale results | `CorpusMappingPanel`; D `8c6c62b`, actual hidden-toggle invalidation |
| XLSX-02 | Direct original import; physical row/column/merge owner through session, edits and exports | Workbook adapters/domain/services; P2/P7 original fixture and readback |
| XLSX-03 | Hidden-sheet control, sparse bounded read, anonymous historical-result ignore without shifting columns | Workbook adapter/UI; native 13/12 sheets, 1118 merges, 51 coordinates and actual KING toggle |
| VIEW-01 | Reference-owner edits and unchanged-total-height segment edits report all affected rows and invalidate exact results | `CorpusViewService`, `CorpusRunPanel`; native service assertions and actual result/export |
| TEXT-01 | Strict BOM/body/UTF-16/NUL validation and preserved empty record boundaries | `TextFileImporter`, UTF-8 utilities; E `1f9d2e0`, 15 external fixtures |
| CMP-01 | One Indel formula in RapidFuzz/fallback; no unsupported Unicode approximation | Similarity/normalizer; Core and Release mathematical goldens |
| CMP-02 | Shared 512MiB peak allocation budget, checked arithmetic, cancellation and predecoded/bounded diff/alignment | Compare core/services; native cancellation, 65536-codepoint fast path, 1000/5000 cases |
| CMP-03 | Four versioned profiles with full custom options, deterministic S/D/I/N, weighted CER/WER and metric-aware UI | F `4c8922b`; 6 strict, 15 metric and 7 alignment goldens; actual CER 300% and custom/restart |
| REPORT-01 | Source/coordinate/parameter/statistics sheets, rich-text reconstruction and Excel limits before publication | Exporter; G `eb48028`, seven original-workbook exports, native + actual OOXML readback |
| IO-01 | Exclusive temporary ownership; real write/flush/close/replace failure boundary preserves original file | `FileIo`; native fault injection with real isolated filesystem delegate |
| TEST-01 | Synchronized competing operations, explicit cache-disabled legacy baselines and new cache-enabled invariants; unique test roots | P2/P4/P5/P6/P7/Core; final Release 7/7 and Core 4/4 |
| BUILD-01 | Shared preset schema 5 matches CMake 3.24 minimum | Actual isolated CMake 3.24.4 and installed 4.4.2 parse tests |

GUI verification additionally fixed native status-label expansion, grid cross-cell text overflow, stale Stopping text and unreadable comparison metric headers. These fixes were rebuilt, separately committed (`98956ba`, `23f5288`, `2908a75`) and visually rechecked. Probe first-ready timing was added in `deaed11`.

## Actual Verification

- Final full Release: 7/7, 437.71 s, including 500 English + 500 Chinese generations, real model switching and Unicode model paths. Reduced Core: 4/4; missing utf8proc capability remains explicitly unsupported.
- Original private workbook: direct native import, 13 sheets / 12 visible / 1118 merges / 51 key coordinates; six mapped sessions and seven native export/readbacks. Original SHA matches preflight.
- Actual isolated program launched with working directory outside the repository, read the original workbook, restored mapping, generated runtime rows, played real English audio, populated cache, recorded a manual result, compared files and exported both views.
- Real WASAPI loopback: 20 requests per available language per baseline/memory/disk mode, 120 recordings total. Head and tail correlation each exceeded 0.7. This is physical loopback evidence, not human listening confirmation.
- Final GUI exports were independently read through ZIP/XML: complete multiline reference, manual mark, physical/reference-owner coordinates and source metadata; CER a/abcd = 300%, NG, correct profile/recipe and three report sheets.
- The 5000 repeated and no-reliable-anchor comparisons each stayed within the shared 446920409-byte budget estimate. Independently observed process peak working set was 442413056 bytes.

## Cache Measurements

Nearest-rank p50/p95 over 20 requests per cell, milliseconds. Baseline means model already loaded, audio cache disabled. Memory and disk hit engine Load/Synthesize deltas were zero; their PCM SHA values matched per sample.

| Language | Baseline p50 / p95 | Memory p50 / p95 | Restart disk p50 / p95 |
| --- | --- | --- | --- |
| en-US | 118.93 / 230.21 | 52.90 / 55.84 | 55.91 / 60.17 |
| zh-CN | 428.03 / 565.11 | 7.48 / 8.22 | 8.73 / 9.45 |

Both hit modes reduced p50 Prepare time by more than 50% on these samples. Restart first-ready from probe entry, including registry scan/cache initialization/first asset verification, was 364.02 ms EN and 176.25 ms ZH. It excludes OS process creation and DLL loading; first validation/Prepare times remain in raw evidence. Probe first-nonzero callback timing includes recorder initialization/pre-roll and is not GUI click-to-audible latency. No universal speedup or ASR recognition quality claim is made.

## Remaining User Gates

- PLAY-04: full cold/warm generate/play/interval/handoff pause/resume/repeated Stop/new Play/close matrix with cancellation-correlated physical output; human listening separately NOT_RUN. Ordinary GUI controls and normal shutdown passed.
- CACHE-07: one-click clear while real audio is queued/generating/playing. Native epoch, retained buffer and partial-failure tests passed; interactive destructive confirmation was not accepted unattended.
- XLSX-04 and VIEW-02: rapid file/sheet/header changes and stale callbacks, plus multi-language edits/ranges/selected source column/cursor combinations. Actual mapping save/build/editor/hidden invalidation and native edit contracts passed.
- CMP-04 and LIFE-01: full cancel/close/reopen during long comparison and each background job type. Native cancel/unwind/join and subsequent compare passed; actual ordinary shutdown had no remaining process.
- ENV-02 evidence limitation: original preflight workbook SHA and isolated populated package/junction/model-copy hashes passed. A complete pre-task hash inventory for every established production/backup file was not made and cannot be recreated retrospectively.
- Native Arabic/Spanish voice gates BLOCKED: downloaded raw Piper assets are not complete accepted sherpa voices. No English substitution or extra model download was performed. Actual recognizer output was not provided; business ASR accuracy is NOT_RUN.

## Evidence And Delivery

Private evidence remains in the external work-package execution directory's `results/`: `local_acceptance_results.json`, `FINAL_REPORT.md`, `cache_timings.csv`, `cache_summary.json`, `loopback_timings.csv`, executable hashes, screenshots, real recordings, exported XLSX, native logs and readback/protection results. It is not a Git payload.

Key local command logs: `logs/review-20260906-delivery-release-tests.log`, `delivery-core-tests.log`, `delivery-package-tests.log`, `delivery-export-xml.log`, `delivery-protection.log`, `measurement-summary.log` (all share the `review-20260906-` prefix).

Verified interactive binary: `dist/review-2908a75-20471e0b55214635904668a687149960/AdayoCorpusTool/AdayoCorpusTool.exe`, SHA256 `7ff64658031812f59d6b4a569ca9c2d867c9e623c87d6c237a041040092cfa05`. Subsequent commits change tests/documentation only. Runtime directories and canonical models were not replaced; no release ZIP was created. Final publication is source-only ordinary commit/push as explicitly requested.
