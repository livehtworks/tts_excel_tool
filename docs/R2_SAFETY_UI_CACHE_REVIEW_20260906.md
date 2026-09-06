# R2 Safety, Interaction And Cache Review

Work package: `TTS-EXCEL-R2-SAFETY-UI-CACHE-20260906`, version 1.0.
Actual baseline: `5b7bee05d0d0518df44bbf684e4bb08ee580fbe2`, matching local HEAD
and remote main at preflight; initial worktree clean. The latest user request
authorizes source commit/push. Canonical resource updates, production executable
replacement and ZIP remain excluded.
Source implementation commit: `5b6aeea`. Verification documentation is committed
separately; Git history is the authority for the final handoff SHA.

## Current Stage

Stages B/C/D/E have source implementations and partial native/GUI verification.
The reported mapping exception is fixed and directly rechecked. This remains
an incomplete whole-work-package acceptance checkpoint, not a release.
The independent baseline
Release regression passed 7/7 in 530.00 seconds, without the private workbook
fixture environment; this is not original-workbook or final R2 acceptance.
The CACHE-01 real-file-lock reproducer failed as expected: valid disk audio
became synthesis when only the metadata replacement failed.

The original workbook SHA matches the work package. The previous 51-coordinate
fixture was found in the external September work package. Baseline GUI imports
the original workbook directly; actual mapping screenshot demonstrates the wide
table and ambiguous automatic-language label. This is not final UI acceptance.

Raw evidence lives outside the repository in the sibling
`_tts_excel_tool_review_r2/20260906-175020` directory. Private screenshots,
workbook fixtures, recordings, configuration and models are not Git payloads.

## Acceptance Matrix

Implementation and verification are independent. No overall PASS is claimed.

| Issue | Implementation | Verification |
| --- | --- | --- |
| SAFE-01 native admission | IMPLEMENTED_NOT_VERIFIED | NOT_RUN |
| SAFE-02 dirty/export revisions | IMPLEMENTED_NOT_VERIFIED | NOT_RUN |
| SAFE-03 playback/session terminal state | IMPLEMENTED_NOT_VERIFIED | NOT_RUN |
| SAFE-04 configuration save ordering | IMPLEMENTED_NOT_VERIFIED | NOT_RUN |
| SAFE-05 recoverable voice preparation | IMPLEMENTED | PASS |
| CACHE-01 LRU write failure | IMPLEMENTED | PASS |
| CACHE-02 shared model load identity | IMPLEMENTED_NOT_VERIFIED | NOT_RUN |
| CACHE-03 cache IO/LRU optimization | IMPLEMENTED_NOT_VERIFIED | NOT_RUN |
| UI-01 voice selection/observations | IMPLEMENTED_NOT_VERIFIED | NOT_RUN |
| UI-02 playback/edit/cache interaction | IMPLEMENTED_NOT_VERIFIED | NOT_RUN |
| UI-03 mapping layout/navigation | IMPLEMENTED_NOT_VERIFIED | NOT_RUN |
| UI-04 compare group draft boundary | IMPLEMENTED_NOT_VERIFIED | NOT_RUN |
| UI-05 report group/diff details | IMPLEMENTED_NOT_VERIFIED | NOT_RUN |
| UI-06 compare option commit/layout | IMPLEMENTED_NOT_VERIFIED | NOT_RUN |
| QA-01 controlled shutdown | IMPLEMENTED_NOT_VERIFIED | NOT_RUN |
| QA-02 evidence/regression | IMPLEMENTED_NOT_VERIFIED | NOT_RUN |

The three special Piper frontends, six historical phoneme warnings and MOSS
native inference remain outside this implementation scope. Their existing
statuses are not upgraded by R2 tests. Historical ENV-02 evidence remains missing.

## Current Evidence

These are subcase results, not full-item or overall acceptance:

- E3 full Release regression: 7/7 PASS in 724.17 seconds, including the original
  private fixtures and 1000 native syntheses. E3 Core: 4/4 PASS. Evidence:
  `native/stage-e3-full-tests.log`, `native/stage-e3-core-tests.log`.
  The later E5 rebuild covers mapping-file failure handling and shutdown-start
  ordering; targeted results are `native/stage-e5-tests.log`. No native adapter
  or TTS/cache implementation changed after the full E3 run.
- E2/E3 P5 uses real locked files to delete five of ten cache entries, retain
  accurate occupancy and a live PCM lease, and preserve unknown files/owner
  identity. A real child exits with code 23 before its dirty LRU timestamps are
  drained; restart reads unchanged valid audio and persists usage safely.
  Evidence: `native/stage-e2-p5-complete.log`. Missing test manifests exposed a
  genuine long-path harness mismatch; original failing logs remain available.
- E4 native Arctic speaker 0/1 alternation: 50 different texts, cache disabled,
  50 syntheses, Load deltas 1 followed by 49 zeros. The first three requests are
  0 -> 1 -> 0. Evidence: `native/stage-e4-speakers.log`; the CSV also retains
  logical IDs, keys, PCM hashes and per-request resource/call counters.
- E1 GUI: `rms` search returns the correct speaker-1 choice without admitting
  other speakers; an entry without an observation remains unverified. Original
  workbook multiline editing, result marks, close/rebuild protection, canceling
  the export path and retaining the edited values/current selection were
  observed. Duplicate tokens are rejected as `MODEL_ADMISSION` while the parent
  stays alive; restoring the isolated tokens restores actual playback and Idle.
  Evidence: `native/ui-e1/`, `native/app-e1/logs/`. These screenshots cover four
  physical models / 23 entries, not the full 2704-entry registry or DPI matrix.
- Source review additionally handles a missing workbook during sheet selection
  as a visible error with invalid analysis. Shutdown now creates its join task
  before canceling services or disabling panels. These last changes are build
  and runtime-test checked, not a claim of corresponding GUI fault injection.
- D7 targeted Release regression: 6/6 PASS, including complete isolated private
  fixtures. Original workbook evidence covers 13 sheets, 12 visible, 1118 merges,
  51 coordinates, six sessions and seven export/readback cases. Logs:
  `native/stage-d7-tests.log`, `native/original-workbook-d3-detail.log`.
- D6 full native regression PASS in 589.12 seconds: Unicode-path initialization,
  admission invalidation, switching, 500 English and 500 Chinese generations.
  Evidence: `native/stage-d6-native-full.log` and its `-detail.log`. The subsequent
  D7 cache batch reduction is covered by targeted P5, not another 1000 syntheses.
  The independent Core build/regression passes 4/4 (`native/core-tests.log`).
- Five malformed-token subprocesses and four malformed-metadata subprocesses
  reject without a fatal exit. Four native valid-voice probes and changed-token
  admission invalidation pass. Evidence: `after/admission-v2/results.json`,
  `metadata-admission/results.json`, `native/admission-valid/`.
- Preparation: 15 real-file/transaction tests PASS, including eight subprocess
  crash boundaries and retention of an unrecognized same-byte temporary file.
  Linked-but-not-renamed replacement/restore crash cases failed before the fix
  (`native/transaction-temp-crash-red.log`) and pass after it
  (`native/transaction-tests-v8.log`). Two actual Ukrainian
  staging/deployed transactions and loaded-native-reader exclusion PASS.
  Evidence:
  `resource_transactions/native/run.log`, `native/native-reader-exclusion-v2.log`.
  A complete prepare run with a real deployed native probe rejecting a NUL
  request restores bytes, paths and identities for both existing and first-install
  cases; a normal rerun succeeds without changing the first recovery journal
  (`native/native-deployed-failure.log`). Both cases also pass under a Chinese
  preparation path (`native/native-deployed-failure-unicode.log`).
- P5 covers shared PCM leases, 130 aliases without repeated weight hashing,
  byte-identical v1 keys, LRU-write failure preservation, and bounded timestamp
  writes with a complete non-retrying drain. Scales 0/100/1000/10000/20000 PASS
  with exact directory accounting and one normal-operation recount before clear
  (`native/cache-scales-d4.log`). The current hit-path batch is two records per 32
  accesses, with remaining dirty timestamps drained at sequence end/shutdown.
- Same-machine performance measurements remain under review. The initial D2
  run and D4 interleaved recheck showed hit-path p95 regressions; they are not
  performance PASS. D7 uses two-record batches and passes all per-sample old-v1
  key/PCM equality and zero Load/Synthesize checks. Ukrainian disk p95 becomes
  22.29 ms versus baseline 20.59 ms (+8.2%). One D7 Amy memory run remains over
  the boundary (116.75 versus 99.78 ms), predominantly resource validation.
  A separate interleaved three-round Amy recheck without further code changes
  measures 103.09 versus 101.97 ms (+1.1%) and disk 124.03 versus 126.62 ms.
  Both measurements are retained; the outlier is not erased or reclassified as
  PASS. Existing resource enumeration/attribute checks remain intact. Evidence:
  `hit-recheck-d7/after/performance-summary.json` and
  `hit-recheck-d7-amy/after/performance-summary.json`. This is not a claim that
  the full cache/device performance composite matrix has passed.
- D5 GUI closes the reported mapping exception. On original `通讯`, first and
  repeated analysis each display 16 columns / 47 rows. Switching to `变更记录`
  displays 5 columns / 13 rows; selecting its last row displays source index 4.
  Header row 100 produces 0 columns / 0 rows with empty details and disabled
  build/voice selection. Evidence: `native/ui-d5/mapping-*.jpg` and `.txt`.
  `AppendRows` previously selected a row before the language/voice side-state
  existed. Population now prepares side-state first and suppresses callbacks
  only while rebuilding, with scoped restoration. No exception is swallowed.
- D2 GUI compared five original Unicode rows and displayed rich-text details.
  D5 confirms that disabling top-level resizing alone still left excessive
  cached pane height. D7 additionally recalculates best sizes after width-aware
  layout. Expanded and re-collapsed screenshots retain a nonzero result grid
  at the same 1898x1219 window size (`native/ui-d7/compare-*.jpg`).

The E4 protected-file after manifest matches all 758 before records (bytes, SHA256
and file identity), covering the scoped canonical program/models and original
workbook (`protection-after-e4.log`). Historical whole-backup evidence is not inferred.
Remaining full-item evidence includes full GUI/DPI/device/lifecycle and composite
cache matrices. `acceptance_results.json` in the private evidence root records
individual work-package assertions separately from the partial native/GUI results.
The current assertion snapshot is 17 PASS / 41 NOT_RUN. NOT_RUN denotes an
unproven complete assertion, even where individual subcases have passed. E5
targeted regression passes 6/6 in 11.72 seconds. Reproduction entry points use
the external run root (`<run>`) and disposable fixture paths:

```powershell
ctest --test-dir <run>/native/build-release -C Release --output-on-failure
ctest --test-dir <run>/native/build-core --output-on-failure
ctest --test-dir <run>/native/build-release -C Release --output-on-failure -E adayo_p4_tts_tests
<run>/native/build-release/adayo_p4_tts_tests.exe --r2-perf <new-cache-root> speakers <isolated-arctic-model.json> <run>/vits-piper-en_US-arctic-medium.samples.json
<run>/protection.ps1 -Phase after
```

Set TEMP/TMP to an existing disposable directory, ADAYO_REVIEW_WORKPACK to the
complete isolated fixture copy, and ADAYO_REVIEW_CACHE_JUNCTION to the explicitly
created disposable junction before tests. Build commands and matching dependency
paths are retained in external `build-native.cmd`, `build-core.cmd` and
`build-only.cmd`; source/executable/evidence SHA256 values are in the assertion
snapshot. Do not regenerate the protection-before baseline.

Active desktop input interrupted further E1 GUI automation. Its unsaved isolated
session was left open, not discarded or forcibly terminated. Remaining acceptance
includes the complete dirty/export-leave flow, actual playback/pause/live-clear
matrix, full registry and multi-group interaction, DPI, and all long-task close
cases. Exact `window_destroyed` instrumentation/evidence is still missing;
`native_returned` and `worker_joined` alone do not establish QA01-D. Performance
outliers remain disclosed above. The source handoff must not be described as all
R2 work completed. No existing runtime was replaced and no ZIP was created.
