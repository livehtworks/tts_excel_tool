# September Review Execution

Baseline: `9e93d1b`. Scope: the 17 issue contracts in the external September 6 work package. The user authorized unattended implementation, staged commits, and final push. Private workbook data and detailed evidence are kept local.

## Stage Gates

- A: create-only package protection, literal copy/hash regression and failed-tool rejection passed. Current CMake parses schema 5. CMake 3.24 execution remains to be checked.
- B: request-owned cancellation and context-aware device handoff implemented. Core and Release P5 passed, including deterministic queued-stale, repeated Stop and handoff controls. Actual audio output and human listening remain separate acceptance gates.
- C: bounded memory/disk audio cache, model/config/asset identity, schema 4, management UI and epoch clear are wired through the real TtsService/PlaybackService. Core P5 and Release P2/P5 passed. Real EN/ZH voices each passed 20 baseline, memory and separate-process disk requests; hits performed zero additional engine loads/syntheses and memory/disk PCM hashes matched. Device/loopback timing, filesystem fault coverage and final performance aggregation remain final-acceptance work.
- D: original workbook native import passed 13 sheets / 12 visible / 1118 merges / 51 golden coordinates and six mapped sessions. Physical coordinates and reference ownership reach runtime rows; merged-reference edits invalidate only covered rows. Unchanged-shape segment edits return all affected rows. Mapping revisions and hidden-sheet filtering are wired in UI. Full GUI interaction and provenance export readback are retained for stage G.
- E-G: pending.

## Local Evidence

- `logs/review-20260906-workpack.log`: external package integrity and independent fixture facts, exit 0; not a product test.
- `logs/review-20260906-preflight.json`: clean baseline, environment and model metadata inventory.
- `logs/review-20260906-stage-a.log`: isolated package regression.
- `logs/review-20260906-stage-b-build.log`, `logs/review-20260906-stage-b-tests.log`: Core build and P5.
- `logs/review-20260906-stage-b-release-build.log`, `logs/review-20260906-stage-b-release-tests.log`: full desktop build and P5.
