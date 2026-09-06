# Voice Resources

Current canonical root: `dist/AdayoCorpusTool/model`. The machine-readable
[inventory](VOICE_RESOURCE_INVENTORY.json) is an audit snapshot, not a second
runtime registry. The application reads `sherpa/*/model.json` through ModelRegistry.

## Inventory

| Item | Count |
| --- | ---: |
| Downloaded Piper model/quality variants | 174 |
| Named locale/voice datasets, before quality variants | 152 |
| Base languages / locale codes | 50 / 55 |
| Speaker slots across all variants, not deduplicated people | 2707 |
| Downloaded variants with native configurations and synthesis checks | 171 |
| Speaker slots synthesized for those variants | 2703 |
| Existing additional Xiao Ya INT8 variant | 1 |
| Registered native configurations / selectable speaker entries | 172 / 2704 |
| MOSS components / files / checked ONNX graphs | 2 / 22 / 8 |

Arabic includes both Kareem quality variants (`ar-JO`). Spanish includes all nine
downloaded variants across `es-AR`, `es-ES` and `es-MX`. Select the actual locale;
the application does not silently alias `ar-SA` to `ar-JO`.

## Unresolved Capabilities

These are not missing downloads and are not enabled by a misleading model.json:

| Downloaded model | Missing native capability |
| --- | --- |
| he_IL-saspeech-medium | Hebrew Nakdimon/niqqud and IPA frontend |
| ja_JA-hi_fi_captain-medium | Japanese OpenJTalk and pitch-accent frontend |
| en_US-mike-medium | Explicit Piper vowel_clusters merging; omitting those clusters would change the required phoneme sequence |

MOSS TTS and audio-tokenizer resources pass ONNX structure and external-weight
range checks. Native multi-graph inference and SentencePiece parity are still not
implemented. Its components are not two voice packs. Completing these missing
engine/frontends is distinct from supplying asset configuration; this task must
not be described as 174/174 voices enabled or as MOSS native support.

## Native Warnings

Six downloaded variants (each original JSON reports piper_version 0.2.0)
synthesize non-silent audio but their original phoneme tables omit symbols
emitted by the current phonemizer:

- `fr_FR-gilles-low`, `fr_FR-mls_1840-low`, `fr_FR-siwis-low`: U+0303 nasal marker.
- `vi_VN-25hours_single-low`, `vi_VN-vivos-x_low`: U+0032 tone marker.
- `zh_CN-huayan-x_low`: U+0032/U+0035 tone markers.

This proves an output/token-table mismatch, not corrupt weights or an
irreparable voice. Historical frontend/version compatibility and the original
reference behavior still need investigation; audible impact is not yet measured.
The original ID maps are preserved; no invented token IDs, substituted voice or
hidden phonemizer fallback is used to suppress these warnings. These six require
listening assessment. Inventory PASS means native load plus finite, non-silent
PCM, not phonetic parity, pronunciation quality or human acceptance.

## Implementation

- Preparation follows upstream Sherpa metadata conventions, preserves original
  ONNX bytes as an exact prefix, and verifies that the inference graph is unchanged.
- 170 prepared weight files and their source paths are NTFS hard links. Phonemizer
  dependencies also share physical files. The existing accepted Amy conversion
  has an inference graph identical to its downloaded original and is retained.
- All candidates undergo actual C++ synthesis for every declared speaker; deployed
  paths undergo another fresh-process synthesis before model.json is published.
- ModelRegistry expands complete speaker lists and rejects missing, duplicate,
  fractional or out-of-range IDs, path escapes and incorrect asset types.
- Character-based Ukrainian uses the existing utf8proc library for explicit NFD
  decomposition plus Sherpa's character frontend. Normalization participates in
  cache identity. Espeak and pinyin models retain unchanged input semantics.
- Desktop and probe executables declare a UTF-8 process code page. The Unicode
  path gate runs before any ASCII espeak initialization; native CLI arguments use
  Windows wide strings. An already initialized espeak process cannot stand in for
  cold-start Unicode-path verification.

## Evidence And Delivery

- Per-model/per-speaker evidence: `logs/voices-all-20260906`,
  `logs/voices-repaired-20260906`, `logs/voices-pt-utf8-20260906`.
- Registry coverage: `logs/voice-registry-inventory.log`, 171 downloaded models,
  2703 speaker entries; 2704 including existing Xiao Ya INT8.
- `_download_test` contains only a historical tokens.txt. It remains a visible
  missing-config diagnostic and is not counted as a downloaded model or deleted.
- Preparation unit contracts: `logs/voice-preparation-tests.log`.
- Resource provenance/hard-link verification: `logs/voice-resource-integrity.json`.
- Final native/reduced-core regressions: `logs/voice-release-utf8-tests.log` and
  `logs/voice-core-tests.log`; current result is summarized in PROJECT_STATUS.md.
- Source and the desktop executable are rebuilt. Existing release executables,
  user configuration, caches, workbooks, backup and the user's running review
  process are not replaced or stopped. EXE-only replacement still awaits approval.
  The subsequent research-handoff request authorizes source commit/push, not
  a release ZIP, packaging operation or runtime replacement.

Current issue/research handoff: [GPT_RESEARCH_HANDOFF_20260906.md](GPT_RESEARCH_HANDOFF_20260906.md).

Preparation/recovery commands and the file contract are in `models/README.md`.
The existing two-model packaging allowlist is unchanged; it does not restrict
runtime ModelRegistry discovery or implicitly authorize redistribution of every
downloaded model.

## Upstream References

- [Sherpa Piper metadata conversion](https://github.com/k2-fsa/sherpa-onnx/blob/v1.13.6/scripts/piper/add_meta_data.py)
- [Piper phoneme types and vowel-cluster configuration](https://github.com/OHF-Voice/piper1-gpl/blob/main/src/piper/config.py)
- [Piper espeak codepoint/cluster processing](https://github.com/OHF-Voice/piper1-gpl/blob/main/src/piper/phonemize_espeak.py)
- [Windows UTF-8 process manifests](https://learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page)
