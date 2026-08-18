# P0 Business Baseline Audit

Stage: P0
Date: 2026-08-18

## Source Evidence

- Old project backup: `backup/old_project_20260818_160848`
- Old Python entry: `backup/old_project_20260818_160848/app.py`
- Old uploaded workbook used as structure reference: `backup/old_project_20260818_160848/uploads/Export VR Command Set V2.2_FullcaseTTS translation_V2.0_20260127.xlsx`
- Frozen business summary: `docs/BUSINESS_BASELINE.md`

## Fixture Added

- `tests/fixtures/adayo_legacy_business_fixture.xlsx`
- `tests/fixtures/p0_business_fixture_expected.json`

The workbook fixture is desensitized. It keeps old workbook structure traits but replaces project-specific utterances with generic feature placeholders.

## Acceptance Assertions Captured

- Workbook has multiple sheets: `Vehicle`, `System`, `EmptySheet`.
- `Vehicle` uses header row 2.
- `System` uses header row 1 as a control sheet.
- `Vehicle` includes multilingual abbreviated headers: `ENG`, `ENU`, `FRF`, `ARG`, `SPM`.
- `Vehicle` includes multiline play cells.
- `Vehicle` includes empty play cells.
- Mapping scenario uses exactly 1 reference column and multiple play columns.
- Existing result-style column is represented by `ENG结果`.
- Expected run view row count is 8 after multiline expansion and empty-row filtering.
- Expected run view column order is index, reference, each play column followed by its result column.
- Result state cycle is `blank -> OK -> NG -> blank`.
- Runtime edit acceptance requires raw row + source column + segment index identity before saving.

## Blocked / Unverified

- `UNVERIFIED_OLD_BEHAVIOR`: Whether old imported result columns should be automatically reused as runtime result marks. The old FastAPI code detects result-like headers but creates runtime result columns from selected play columns and resets result marks when applying a new view. This fixture therefore records old result columns as analysis evidence only, not as automatic imported marks.
- `UNVERIFIED_OLD_BEHAVIOR`: Exact UI rendering behavior for large real worksheets is not captured by the fixture. Performance acceptance remains in later stages.

## Forbidden-Scope Check

- No Core source code was modified for P0 fixture creation.
- No old original Excel file was modified.
- No Python runtime dependency was added to the C++ release design; `openpyxl` was used only as a development-time fixture generator.
