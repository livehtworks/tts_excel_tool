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
    model.json
    ...
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

Paths are relative to the voice directory unless absolute. `model` and `tokens` are required. `data_dir`, `lexicon`, and `rule_fsts` are optional but must exist when provided.

UI and TTS services must use `ModelRegistry`; language/voice paths must not be hardcoded in UI code.

`package-manifest.json` is copied to `dist/AdayoCorpusTool/model` for release/runtime validation. `scripts/package_windows.ps1` validates the sherpa model ids listed there against `dist/AdayoCorpusTool/model/sherpa`.

The package script never downloads models. Use `scripts/download_model_resources.ps1` for domestic no-proxy downloads before packaging or testing release/runtime model behavior.
