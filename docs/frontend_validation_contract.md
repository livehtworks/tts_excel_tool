# Native Frontend Validation Contract

Scope: pinned Sherpa-ONNX 1.13.6 VITS adapter and the ONNX Runtime supplied in
that same native package. This is admission of known resource-error families,
not process isolation from arbitrary native failures. Current implementation
and execution evidence are tracked in `R2_SAFETY_UI_CACHE_REVIEW_20260906.md`.

## Branches

The runtime reads actual graph metadata through the matching ONNX Runtime C
API before calling `SherpaOnnxCreateOfflineTts`. Required metadata is
`sample_rate`, `n_speakers`, `language`, and `comment`. Integer fields must be
complete nonnegative int32 values; binary flags must be 0 or 1. Sample rate
must be 8000..384000 Hz and speaker count positive. Configured and requested
speakers must be within that model's range. Optional metadata defaults remain
the pinned upstream defaults.

| Branch | Token contract | Required symbols / dependencies |
| --- | --- | --- |
| `frontend=characters` | One Unicode scalar per ordinary symbol; `<PAD>`, `<BOS>`, `<EOS>`, `<BLNK>` may name special IDs | Actual enabled BOS/EOS/blank IDs must occur in the table; existing explicit NFD remains unchanged |
| `jieba` or `has_g2pw` | Whitespace-delimited symbols, including multi-codepoint pinyin syllables | Lexicon required; g2pW uses `_`, `^`, `$` |
| Piper/eSpeak | One Unicode scalar per ordinary symbol; upstream `<BLNK>` exception retained | Piper requires `_`, `^`, `$`; nonempty voice metadata and espeak core data files |
| Other existing lexicon branch | Whitespace-delimited symbols | Lexicon required; no claim of complete coverage of other upstream model families |

Token files are read as bytes, reject embedded NUL and invalid UTF-8, and
require symbol plus a complete nonnegative int32 ID on each nonempty line.
Duplicate symbols and extra fields are rejected. CRLF is supported, as is the
literal space token (`  6`). IDs are not renumbered; duplicate numeric IDs are
not confused with duplicate symbols. Admission never removes a token.

The adapter caches successful metadata/front-end admission by a digest of the
actual model, tokens, lexicon, FSTs, recursive data files and relevant paths.
Only successful checks enter that bounded cache. `Load` recomputes this digest;
normal audio cache hits retain TtsService's existing directory and file-identity
validation and do not create a new ONNX session. First-load preflight is part
of measured model-load cost, not reported as warm-hit cost.

## Remaining Fatal Families

`BLOCKED_UPSTREAM_FATAL_PATH` applies individually to:

- Native allocation failure, access violations, provider/kernel defects and
  arbitrary graph execution failures beyond the safe ORT session-read boundary.
- Learned embedding vocabulary bounds not exposed by the current ORT metadata
  API: the gate proves int32 syntax/range and special-ID membership, not that an
  arbitrary ID is within a hidden initializer's learned vocabulary.
- Corrupt FST contents and espeak binary contents: existence, identity and
  nonempty core files do not prove every upstream binary parser path safe.
- External alteration between validation and native use. Resource identity
  invalidation is not an OS sandbox or protection against hostile writers.
- Other upstream frontends outside this project's prepared Piper, pinyin and
  character resources. No new frontend or inference subprocess is introduced.

The process-fatal cases must be exercised in disposable child processes.
Child exit codes do not by themselves establish desktop GUI survival.

## Pinned Sources

- [VITS metadata reader](https://github.com/k2-fsa/sherpa-onnx/blob/v1.13.6/sherpa-onnx/csrc/offline-tts-vits-model.cc)
- [Frontend branch selection](https://github.com/k2-fsa/sherpa-onnx/blob/v1.13.6/sherpa-onnx/csrc/offline-tts-vits-impl.h)
- [Piper token reader](https://github.com/k2-fsa/sherpa-onnx/blob/v1.13.6/sherpa-onnx/csrc/piper-phonemize-lexicon.cc)
- [Character token reader](https://github.com/k2-fsa/sherpa-onnx/blob/v1.13.6/sherpa-onnx/csrc/offline-tts-character-frontend.cc)
- [Pinyin lexicon](https://github.com/k2-fsa/sherpa-onnx/blob/v1.13.6/sherpa-onnx/csrc/character-lexicon.cc)
- [Matching ONNX Runtime C API](https://github.com/microsoft/onnxruntime/blob/v1.27.1/include/onnxruntime/core/session/onnxruntime_c_api.h)
