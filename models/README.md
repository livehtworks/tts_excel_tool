# models

模型目录只存模型和 metadata，不在 C++ 中硬编码语言路径。

运行发布目录约定：

```
dist/AdayoCorpusTool/model/
  sherpa/
    <voice-id>/
      model.json
      *.onnx
      tokens.txt
      espeak-ng-data/   # 某些 Piper/VITS voice 需要
  moss/
    MOSS-TTS-Nano-100M-ONNX/
    MOSS-Audio-Tokenizer-Nano-ONNX/
  piper/
    voices/
      <language>/<locale>/<voice>/<quality>/*.onnx
      <language>/<locale>/<voice>/<quality>/*.onnx.json
```

Sherpa `model.json` schema:

```json
{
  "id": "vits-piper-en_US-amy-low",
  "display_name": "English US Amy Low",
  "engine_id": "sherpa-vits",
  "language_code": "en-US",
  "model": "en_US-amy-low.onnx",
  "tokens": "tokens.txt",
  "data_dir": "espeak-ng-data",
  "lexicon": "",
  "rule_fsts": "",
  "speaker_id": 0,
  "num_threads": 2
}
```

All asset paths must be relative and contained within the voice directory. Absolute paths, parent escapes and escaping links are rejected. `model` and `tokens` are required. `data_dir`, `lexicon`, and `rule_fsts` are optional but must exist when provided.

Multi-speaker configurations additionally declare `num_speakers` and a complete
`speakers` array of `{ "id": 0, "name": "speaker name" }` records. IDs must cover
`0 .. num_speakers-1` exactly once. `speaker_id` selects the default. ModelRegistry
keeps the base model ID for the default and exposes other speakers as
`<model-id>::speaker-<id>` to the existing language/voice selector. They share
assets, but retain distinct speaker IDs and audio cache identities.
Character-based Piper voices use `text_normalization: "nfd"`; the adapter applies
Unicode canonical decomposition before Sherpa's character frontend. Other voices
use `none`. The normalization mode is part of the model/cache identity.

UI and TTS services must use `ModelRegistry`; language/voice paths must not be hardcoded in UI code.

`package-manifest.json` is copied to `dist/AdayoCorpusTool/model` for release/runtime validation. `scripts/package_windows.ps1` validates the sherpa model ids listed there against `dist/AdayoCorpusTool/model/sherpa`.

The package script never downloads models. Use `scripts/download_model_resources.ps1` for domestic no-proxy downloads before packaging or testing release/runtime model behavior.

## Preparing Downloaded Voices

`scripts/prepare_downloaded_voices.py` is development-time resource preparation,
not a Python runtime dependency. It uses the ONNX parser and the native
`adayo_p4_tts_tests --voice-probe` entry point. It never downloads models or packages
an application. Run it with an explicit canonical root, built native probe,
`tests/fixtures/voice_smoke_samples.json` and a new, disposable report directory.

- Every candidate is synthesized by the production C++ TtsService/Sherpa adapter
  before registration. All declared speakers are checked for finite, non-silent
  PCM; the deployed paths are then checked again before publishing `model.json`.
- ONNX metadata is appended using the protobuf serializer. Original bytes remain
  an exact prefix, and the parsed inference graph must remain identical. A
  `.onnx.sherpa-metadata.json` journal records original length/hash and prepared
  hash. The downloader recognizes that journal and refuses to overwrite a
  changed prepared file.
- Runtime `prepared.onnx` and phonemizer dependencies are regular NTFS hard links,
  not symlinks/junctions or copies. There is one physical copy of newly prepared
  weights under the canonical model root. Do not edit either link independently.
- Existing accepted converted voices are not deleted. `_download_test` is an
  incomplete historical resource directory, not a usable voice.
- MOSS graph/external-weight integrity is checked separately. Its two components
  are not voice packs, and resource completeness does not enable the unfinished
  native MOSS adapter.
- A native synthesis smoke proves load/generation, not pronunciation, prosody or
  listening acceptance. Unsupported text frontends remain explicit blockers.

Recovery is manual: stop the application, verify a prepared file against its
journal, recover its original prefix to a new temporary file, verify the original
hash, and atomically restore the source. Restore any previous model configuration
from the per-run `.before` snapshot and rebuild its hard links explicitly. Do not
truncate a live linked file. No automatic fallback or destructive cleanup runs.
