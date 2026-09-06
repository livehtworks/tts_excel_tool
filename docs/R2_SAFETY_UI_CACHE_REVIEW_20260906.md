# R2 Safety, Interaction And Cache Review

Work package: `TTS-EXCEL-R2-SAFETY-UI-CACHE-20260906`, version 1.0.
Actual baseline: `5b7bee05d0d0518df44bbf684e4bb08ee580fbe2`, matching local HEAD
and remote main at preflight; initial worktree clean. The latest user request
authorizes source commit/push. Canonical resource updates, production executable
replacement and ZIP remain excluded.
Initial R2 implementation commit: `5b6aeea`; native cancellation and destruction
tracing continued in `303abbb`; native identity/performance coverage is in
`0e3c1e8`. Git history is the authority for subsequent
staged fixes and the final handoff SHA.

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
| CACHE-02 shared model load identity | IMPLEMENTED | PASS |
| CACHE-03 cache IO/LRU optimization | IMPLEMENTED_NOT_VERIFIED | NOT_RUN |
| UI-01 voice selection/observations | IMPLEMENTED_NOT_VERIFIED | NOT_RUN |
| UI-02 playback/edit/cache interaction | IMPLEMENTED_NOT_VERIFIED | NOT_RUN |
| UI-03 mapping layout/navigation | IMPLEMENTED_NOT_VERIFIED | NOT_RUN |
| UI-04 compare group draft boundary | IMPLEMENTED_NOT_VERIFIED | NOT_RUN |
| UI-05 report group/diff details | IMPLEMENTED | PASS |
| UI-06 compare option commit/layout | IMPLEMENTED_NOT_VERIFIED | NOT_RUN |
| QA-01 controlled shutdown | IMPLEMENTED_NOT_VERIFIED | NOT_RUN |
| QA-02 evidence/regression | IMPLEMENTED_NOT_VERIFIED | NOT_RUN |

The three special Piper frontends, six historical phoneme warnings and MOSS
native inference remain outside this implementation scope. Their existing
statuses are not upgraded by R2 tests. Historical ENV-02 evidence remains missing.

## Current Evidence

These are subcase results, not full-item or overall acceptance:

- F11 observes the existing F10 executable without changing production code.
  SAFE02-A passes: reference F2 receives the copied multiline cache diagnostics
  while preserving the original four-line suffix, English G2 gains the prefix
  `R2 F11 edit. `, and its result is marked OK. Both Close and Generate prompt;
  Cancel preserves the edited values, result, G2 result selection/source details,
  and 1-47 range. Cancelling the save path after export-then-close also retains
  the session and does not close. This closes only the path-cancel part of
  SAFE02-C; GUI export failure remains unverified.
  SAFE02-D passes: accepting unchanged F2 text and regenerating does not prompt;
  F9 playback-only close also did not prompt. Export-then-close on F10 creates
  `native/ui-f11/edited-runtime-close.xlsx` before controlled exit. Production
  OpenXlsxWorkbookReader readback reconstructs the exact three effective GUI
  edits from the untouched original and compares all 47x8 cells plus 376 source
  mappings. Independent ZIP/XML checks confirm copied text, exactly one result,
  source hash and owner coordinates. Export SHA256:
  `57e2edf354e43f688b94f363f04f7d79b7ada9b40210019afe8aa91a64aa8fb8`.
  Commands/expected values are in external `native/ui-f11/readback.cpp`,
  `build-readback.cmd`, `check-xml.ps1`; results are `native-readback.log` and
  `xml-readback.log`. Screenshots in that directory retain each prompt/cancel
  and the actual detail-copy roundtrip. F10 shutdown logs record joined workers
  before native window destruction; this is not active-native cancellation.
  F11 targeted Release regression passes 6/6 in 8.03 seconds, excluding only
  the unchanged long P4 suite already exercised by F5; full output is retained
  in `native/stage-f11-targeted-tests.log`.
- F11 actual paused-playback clear on F9: 45 managed entries are removed with
  zero failures, memory ownership drops from 5760372 to zero, and the paused
  device's 43008-byte external lease remains alive. Owner-only disk usage is 47
  bytes, dirty records drop from 7 to zero and config bytes remain unchanged.
  Resume completes the retained item and synthesizes subsequent items. The log
  records request 2/item 6 as a memory hit with zero load/synthesis, followed by
  item 7/8 synthesis. Its 96833.8821 ms completion includes the deliberate pause,
  not a hot-cache latency sample. No new loopback recording is claimed. Queued,
  generating, partial-delete-failure and quota-shrink device combinations remain
  unverified; neither UI02-C nor CACHE03-B is promoted. Evidence:
  `native/ui-f11/{paused-before-clear,lease-after-clear,resume-after-clear,next-item-after-clear}-*.jpg`
  and `native/app-f9-full/logs/AdayoCorpusTool_20260907.log`.

- F9 GUI closes UI02-A/D. A real Windows read-sharing handle without DELETE
  sharing on the isolated config causes atomic replacement to fail. Unapplied
  quota 2049 leaves the 2048 MiB effective limit and config bytes unchanged;
  failed Apply restores 2048 and the same bytes. F8 first reproduces the error
  disappearing when playback starts. The F9 fix retains the settings error
  separately through cold synthesis, real playback and pause. Unlock plus Apply
  during playback succeeds and clears it. The paused device holds 123904 bytes
  in the observed external lease; this is not live-clear acceptance.
  Pause at item 4, scroll to rows 12-29 and select reference F23, disable follow,
  resume across subsequent items and scroll to rows 30-47: view/selected source
  stay user-controlled. Reenable follow at item 24: subsequent item positioning
  brings the playback cell into view without changing F23/owner21 details.
  Stop at item 45 returns Idle and removes only the playback highlight.
  Screenshots: `native/ui-f1/f8-cache-*.jpg` and `native/ui-f1/f9-*.jpg`.
- F10 replaces the native cache message box, which visibly abbreviates long
  paths, with a resizable, scrollable read-only text dialog. It includes the
  actual AudioCache-owned root, complete warnings and saved-setting errors.
  The existing P5 capacity test now verifies the root accessor against its
  independently allocated cache directory. F10 Release build passes; targeted
  Release is 6/6 in 8.84 seconds and Core 4/4 in 4.90 seconds. Logs:
  `native/stage-f10-build.log`, `native/stage-f10-targeted-tests.log`,
  `native/stage-f10-core-build.log`, `native/stage-f10-core-tests.log`.
  Built EXE `native/app-f10-full/AdayoCorpusTool.exe`, SHA256
  `36F00190E086816B8BEAEB86F780FD1950EBAE99EF9230A59D2B422D957C6993`.
  F11 verifies the actual F10 dialog fits its parent, wraps the full cache path
  without ellipsis, and copies all diagnostics into the real multiline editor.
  Exact readback from the subsequently exported GUI session confirms no path or
  text loss. This is one observed window size, not the full DPI matrix.

- F8 comparison GUI and OOXML readback close UI04-A/C, UI05-A/B/C and
  UI06-B/C. F1 two-group exports retain both groups and all three sheets when
  viewing group two. An unapplied path edit disables Start; Apply invalidates
  the old export and subsequent readback changes only that group's source.
  Navigation leaves configuration bytes and the report unchanged. Logs:
  `native/ui-f1/{original-readback,applied-readback-v2}.log`.
- F6/F7 add row metric/status/S/D/I/N, imported record number and source
  filename/path/hash. Indel row and group edit statistics are N/A, matching
  export, and normalized OK retains raw red differences. P6 checks both
  fragment reconstructions for every supplied strict/metric fixture. Actual GUI
  covers combining accents, Arabic, emoji and wrapped long text; rich-detail
  copy/paste yields plain emoji/Chinese, not markup. Legacy record numbers are
  positions after its existing skip-empty import, not physical file lines.
  No import, alignment, normalization or scoring algorithm changed.
  Four-metric export checks: `native/ui-f5/readback-v3.log`; screenshots:
  `native/ui-f1/{strict-long-detail,strict-rtl-detail,emoji-plain-paste,
  f7-indel-group-na,f7-indel-raw-diff}-0.jpg`.
- Strict Rows disables and retains NFC settings; composed/decomposed raw text
  remains NG. Strict Sequence enables the normalizer only for pairing and says
  so explicitly. Initial summary wrapping now starts from an unwrapped label at
  the actual width. F8 also fixes stale error status after invalid-to-valid
  recovery. Actual `nan` input prevents Start and retains config bytes; 175%
  is saved only on blur as 1.75; focused 200% is captured by Start and exported
  as 2. Path typing retains config bytes and last-write time. Expanded/collapsed
  advanced controls preserve report/export validity. F8 export readback checks
  every cell in all three sheets against the earlier verified CER report,
  permitting only the specified threshold/custom/status changes. Its initial
  harness missed the duplicate status column in the statistics sheet; that
  failed log is retained, and corrected expected status coverage passes.
  Evidence: `native/ui-f5/{config-f8-witness.json,readback-f8-v2.log}` and
  `native/ui-f1/f8-*-0.jpg`. Strict-custom and CER-custom process restarts pass;
  remaining preset GUI restart combinations keep UI06-A unverified.
- F8 desktop build and targeted Release pass 6/6 in 11.41 seconds, following
  the full F5 native suite. Latest isolated EXE SHA256:
  `bf67842297aead27b38e7ae625838e03d2cc688e5ab61599ad08c5c2785f435a`.
  Logs: `native/stage-f8-{ui-build,targeted-tests}.log`.

- F5 full Release passes 7/7 in 711.48 seconds. Its 699.58-second P4 run includes
  real shared-load/SID, rule/thread reload, pinyin/eSpeak/NFD, cancellation at
  Load/Synthesize return and 1000 EN/ZH generations. The actual request ID and
  shared load identity are now recorded and asserted, including legal hits.
  Frozen old-v1 cache key/PCM readback across four voices passes in F4 and F5,
  all hits with zero Load/Synthesize calls. Together with E4's cache-disabled
  50 alternating-SID requests this establishes CACHE02-A/B/C/D. Evidence:
  `native/stage-f5-full-tests.log`, `native/stage-f5-full-detail.log`,
  `hit-recheck-f5/{baseline,after}/performance/`.
- F5 config coverage repeats full Runtime reconstruction three times after the
  serialized concurrent-field and actual Windows sharing-lock failures. F7
  Core passes 4/4 in 4.87 seconds (`native/stage-f7-core-tests.log`).
- F4 replaces redundant ordered-map digest lookups with one bounded hash-table
  lookup; full directory traversal, attribute/file-identity checks, ordered
  hashes and v1 key bytes remain unchanged. F4's three rounds (150 requests per
  voice/mode/phase) have no aggregate p95 increase over 10%. F5 appends actual
  load identity diagnostics and reruns the same frozen samples. Amy disk
  aggregate p95 is 144.25 -> 175.30 ms. Its per-round baseline/after p95 values
  are 149.70/183.19, 138.55/149.65 and 144.25/133.96 ms: not a persistent
  three-round increase. First-round resource-validation mean is 103.46 ->
  124.70 ms; lookup means are 16.85 -> 17.49, 16.99 -> 12.82 and 17.23 ->
  11.26 ms. Resource validation remains the measured bottleneck; no causal
  claim about OS noise or a final CACHE03-D PASS is made. Metadata writes fall
  from 150 to 18 (memory) / 6 (disk), with no recounts or hash-byte rereads.
  Both runs and earlier outliers remain in `hit-recheck-f4/` and
  `hit-recheck-f5/`, including their `after/performance-summary.json` files.

- F1/F3 continuation: desktop WM_NCDESTROY timestamps close the previously
  missing diagnostic implementation. Idle and real memory-hit paused playback
  exit completely. Paused cancellation -> worker joined is 65.17 ms, and
  cancellation -> native window destroyed is 446.10 ms. Evidence:
  `native/ui-f1/paused-close-0.jpg`, `native/app-f1-full/logs/`.
  Native load/slow-synthesis close still needs its corresponding GUI evidence.
- The user confirms normal listening for the 47-row English sequence. Full
  registry GUI has 2704 entries, selects Amy and renders LibriTTS speakers.
  Follow-off playback preserves the manually scrolled viewport; paused follow
  activation does not jump. Evidence: `native/ui-f1/`. These are not all-voice
  listening or DPI claims.
- F2 real Sherpa contracts reproduced canceled Load continuing into Synthesize
  (`native/stage-f2-native-contracts-red.log`, assertion
  `!synthesized_after_cancel`). F3 checks cancellation again after Load and the
  same native case passes (`native/stage-f3-native-contracts.log`). It also
  verifies different SID keys/PCM, same-SID hits, thread/model/rule reload,
  pinyin/eSpeak/NFD, speed clamping and no fill when canceled at native return.
- F3 runtime/config: a real Windows handle denies config replacement. Bytes and
  effective settings remain unchanged, retry succeeds after unlocking, and a
  new Runtime reloads the latest independent fields. Evidence:
  `native/stage-f3-workbook-runtime.log`. F3 targeted Release is 6/6 in 7.94 s
  (`native/stage-f3-tests.log`), separate from the real native contract test.
- F1 repeats three rounds of 50 old-v1 memory/disk keys for four voices. Keys
  and PCM match baseline, all hits have zero engine calls. Amy memory p95 is
  104.38 -> 117.09 ms, still above the investigation threshold. Its lookup mean
  drops to about 0.60 ms but resource validation dominates. Retain this result
  with older outliers; no CACHE03-D PASS yet. Evidence:
  `hit-recheck-f1/after/performance-summary.json` and its per-round CSV logs.

- E3 full Release regression: 7/7 PASS in 724.17 seconds, including the original
  private fixtures and 1000 native syntheses. E3 Core: 4/4 PASS. Evidence:
  `native/stage-e3-full-tests.log`, `native/stage-e3-core-tests.log`.
  The later E5 rebuild covers mapping-file failure handling and shutdown-start
  ordering; targeted results are `native/stage-e5-tests.log`. Later TTS/cache
  changes are covered by the full F5 run above, not inferred from E3.
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

The F11 protected-file after manifest matches all 758 before records (bytes, SHA256
and file identity), covering the scoped canonical program/models and original
workbook (`protection-after-f11.log`). Historical whole-backup evidence is not inferred.
Remaining full-item evidence includes full GUI/DPI/device/lifecycle and composite
cache matrices. `acceptance_results.json` in the private evidence root records
individual work-package assertions separately from the partial native/GUI results.
The reconciled assertion snapshot is 33 PASS / 25 NOT_RUN. Each NOT_RUN now names
its specific missing combination or unresolved measurement; it is not a generic
claim that source work or tools are blocked. NOT_RUN denotes an
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

Active desktop input interrupted E1 GUI automation; that session was not discarded
or forcibly terminated by the agent. It was absent when this continuation resumed.
F1/F5/F6/F7/F8/F9/F10 windows closed normally. F11 desktop automation paused on
detected user input and resumed after explicit user confirmation. F10's edited
session was exported before closing, and its detail dialog was visually checked.
Remaining acceptance
includes export-failure leave protection, the complete playback/pause/live-clear
matrix, full registry and multi-group interaction, DPI, and all long-task close
cases. Actual `window_destroyed` instrumentation now exists, with idle and paused
close evidence. The active-native four-phase trace still needs verification;
`native_returned` and `worker_joined` alone do not establish QA01-D. Performance
outliers remain disclosed above. The source handoff must not be described as all
R2 work completed. No existing runtime was replaced and no ZIP was created.
