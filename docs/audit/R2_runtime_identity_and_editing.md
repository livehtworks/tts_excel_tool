# R2 Runtime Identity And Editing Remediation

Status: completed.

## Fixed

- Runtime result marks now use `ResultIdentity`:
  - `raw_row_index`
  - `play_source_column`
  - `segment_index`
- Result cycling no longer keys by transient display row.
- Result cycling updates only the current result cell and does not rebuild the full view.
- Synthetic blank Play cells are rejected by `CorpusViewService::UpdateDisplayCell`.
- Synthetic blank Result cells cannot receive OK/NG marks.
- Structural Play edits invalidate only affected result marks for the same raw row/play column from the changed segment onward.
- Non-structural edits update the source cell and current displayed cells without full service rebuild.
- Runtime UI marks synthetic blank Play cells and Result cells read-only; result double-click updates the current cell only.

## Evidence

- R2 tests cover:
  - result marks surviving earlier-row segment deletion;
  - duplicate text not being used as identity;
  - synthetic blank edit rejection;
  - synthetic blank result rejection.
- `windows-core` Debug tests passed: `4/4`.
- `windows-release` Release tests passed: `7/7`.

## Remaining Later Stages

- R3 still must wire playback UI controls and improve PlaybackService pause/stop semantics.
- R4 still must move large compare/export/import work to a background queue.
