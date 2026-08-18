# R1 Mapping, Config, And Model Registry Remediation

Status: completed.

## Fixed

- Result-column recognition now detects:
  - `测试结果`
  - exact `result/results`
  - language token or locale plus Chinese suffix `结果`, such as `ENG结果`.
- Default analyzer mapping now keeps only utterance columns enabled as Play.
  - Meta, Result, and Unknown default to disabled Ignore.
  - Reference must be selected explicitly.
- Mapping/config data now carries stable `tts_model_id`.
- Config schema is upgraded to v2.
  - v1 migrated language values are not treated as user overrides.
  - only `language_user_overridden=true` can override current analyzer language.
- Workbook identity no longer includes file size or mtime.
  - identity is normalized path only.
  - sheet/header row preferences can be stored by workbook+sheet.
- Model registry scanning now returns valid model entries plus invalid diagnostics.
  - one bad model directory no longer blocks valid voices.
- Mapping UI now uses grid editors for enabled flag, role, language, and model id instead of requiring internal enum text.
  - selecting a new Reference cancels any prior Reference.
  - batch select/all and batch role controls are present.

## Evidence

- `windows-core` Debug tests passed: `4/4`.
- `windows-release` Release tests passed: `7/7`.
- R1 tests cover `ENG结果`, default mapping roles, v1 config migration, user language override, stable workbook identity, header row lookup, `tts_model_id` persistence, and model registry diagnostics.

## Remaining Later Stages

- R3 still must wire playback UI to column-bound `tts_model_id`.
- R5 will harden config atomic writes and packaging manifest behavior.
