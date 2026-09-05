# September Review Execution

Baseline: `9e93d1b`. Scope: the 17 issue contracts in the external September 6 work package. The user authorized unattended implementation, staged commits, and final push. Private workbook data and detailed evidence are kept local.

## Stage Gates

- A: create-only package protection, literal copy/hash regression and failed-tool rejection passed. CMake 3.24.4 (upstream SHA verified, portable isolated tool) and current CMake both parse the shared schema 5 presets.
- B: request-owned cancellation and context-aware device handoff implemented. Core and Release P5 passed, including deterministic queued-stale, repeated Stop and handoff controls. Actual audio output and human listening remain separate acceptance gates.
- C: bounded memory/disk audio cache, model/config/asset identity, schema 4, management UI and epoch clear are wired through the real TtsService/PlaybackService. Core P5 and Release P2/P5 passed. Real EN/ZH voices each passed 20 baseline, memory and separate-process disk requests; hits performed zero additional engine loads/syntheses and memory/disk PCM hashes matched. Device/loopback timing, filesystem fault coverage and final performance aggregation remain final-acceptance work.
- D: original workbook native import passed 13 sheets / 12 visible / 1118 merges / 51 golden coordinates and six mapped sessions. Physical coordinates and reference ownership reach runtime rows; merged-reference edits invalidate only covered rows. Unchanged-shape segment edits return all affected rows. Mapping revisions and hidden-sheet filtering are wired in UI. Full GUI interaction and provenance export readback are retained for stage G.
- E: Core and Release Core/P6 passed; all 15 external encoding/record fixtures passed in Release. Both builds use the same Indel goldens. Unsupported Unicode mapping is explicitly rejected by the reduced build. Shared peak budget covers scores/DP/trace/Unicode/output/diff, cancellation unwinds reservations and allows a subsequent comparison, and the 65536-codepoint common-prefix diff fast path passed. Release 1000/5000-row regressions passed.
- F: four versioned profiles, deterministic unit edit statistics, weighted CER/WER, raw strict equality, empty-record row pairing, complete schema 4 option persistence and metric-aware UI are implemented. Release Core/P2/P6 passed, including external math/metric/pairing goldens. Legacy threshold migration is retained; old root threshold values are migration provenance, not a second active UI authority.
- G: implemented provenance/parameter/statistics export, Excel limit checks, atomic IO fault boundary, native fault tests, process-unique test roots and real WASAPI loopback probes. Release six non-P4 tests passed, including native original-workbook export/readback and external strict/CER/WER fixtures. Actual ZIP/XML red-run reconstruction passed. EN 20-sample baseline/memory/disk loopback passed; final ZH, optimized timings, voice stress and isolated GUI acceptance are in progress.

## Local Evidence

- `logs/review-20260906-workpack.log`: external package integrity and independent fixture facts, exit 0; not a product test.
- `logs/review-20260906-preflight.json`: clean baseline, environment and model metadata inventory.
- `logs/review-20260906-stage-a.log`: isolated package regression.
- `logs/review-20260906-stage-b-build.log`, `logs/review-20260906-stage-b-tests.log`: Core build and P5.
- `logs/review-20260906-stage-b-release-build.log`, `logs/review-20260906-stage-b-release-tests.log`: full desktop build and P5.
