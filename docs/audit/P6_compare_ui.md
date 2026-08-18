# P6 Compare UI Audit

Date: 2026-08-18

## Scope

- Added fixed comparison regression tests.
- Connected Compare UI to `CompareService`.
- Kept `TextNormalizer`, `TextSimilarity`, `SequenceAligner`, and `CharacterDiff` boundaries unchanged.

## Acceptance

Automated coverage includes:

- Exact match.
- One Chinese character replacement.
- Missing reference sentences.
- Extra actual sentence.
- Three consecutive missing reference sentences.
- Repeated short sentences.
- Chinese, English, and Arabic.
- Full-width/half-width normalization through utf8proc.
- Case folding.
- Punctuation ignore on/off.
- Unique reference/actual index ownership.
- 1000-row performance smoke.

## Commands

```cmd
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
cmake --build --preset windows-release
ctest --preset windows-release -R adayo_p6_compare_tests --output-on-failure
```

Result:

```text
adayo_p6_compare_tests PASS
P6 1000x992 compare elapsed_ms recorded by test output
```

## UI

`ComparePanel` now supports:

- Reference text file import.
- Actual text file import.
- Separate alignment and OK thresholds.
- Punctuation ignore toggle.
- Result grid with reference index, actual index, texts, similarity, and status.

## Forbidden-Scope Check

- Raw text is not mutated.
- Diff remains codepoint-based.
- Alignment threshold and pass threshold remain separate.
- One actual row cannot match multiple reference rows.
