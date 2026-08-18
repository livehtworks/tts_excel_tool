# R6 Final Acceptance

Date: 2026-08-18

## Status

Local automated R6 acceptance passed on the development workstation. Target Windows PC manual acceptance has not been executed in this session, so `P9 accepted` is not restored.

## Commit

The package source commit is generated after this audit checkpoint and recorded in the release package manifest.

## Automated Verification

- `git diff --check`
  - passed
- `rg "assert\\(" tests`
  - no matches
- `cmake --build --preset windows-core --config Debug`
  - passed
- `ctest --preset windows-core --output-on-failure`
  - 4/4 passed, 21.32 seconds
- `cmake --build --preset windows-release --config Release`
  - passed
- `ctest --preset windows-release --output-on-failure`
  - 7/7 passed, 412.87 seconds

## Covered Checks

- P2 legacy workbook fixture and Chinese path workbook read.
- P4 sherpa native TTS with speed semantics, 20 EN/ZH switches, 500 English synth, and 500 Chinese synth.
- P5 pause during generation, stop during interval, and stale worker state protection.
- P6 Chinese, English, Arabic, NFKC, Unicode punctuation, UTF-8/UTF-16 text import, and 1000/5000-row performance.
- P7 runtime export and multi-group comparison XLSX export/readback.
- Config v1 migration, corrupt config backup, and future schema overwrite refusal.
- Runtime stable result identity, synthetic blank rejection, and structural edit invalidation.

## Performance Evidence

- P6 1000x992 compare: 258 ms.
- P6 5000x4990 compare: 6514 ms.
- English sherpa 500 synth: elapsed 32282 ms, average 64.068 ms, p95 79 ms.
- Chinese sherpa 500 synth: elapsed 200712 ms, average 400.938 ms, p95 456 ms.

## Target PC Manual Acceptance

Not executed here. The checklist requires validation on a target Windows PC with no VS, no Python, no Node, and no discrete GPU. P9 remains revoked until those manual items pass against the packaged release.

## Known Limitations / Blocked

- `MossNanoTtsEngine` remains intentionally blocked as `MOSS_PORT_BLOCKED`.
- Target-machine manual UX/audio acceptance is pending user execution.
