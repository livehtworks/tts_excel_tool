# P4 Model Resource Audit

Stage: P4 preparation only
Date: 2026-08-18

## Local Existing Resources

Old archived project contains Piper ONNX voices under:

`backup/old_project_20260818_160848/model/piper/voices`

Confirmed candidate voice files:

- `zh_CN-huayan-medium.onnx`
- `zh_CN-huayan-medium.onnx.json`
- `zh_CN-xiao_ya-medium.onnx`
- `zh_CN-xiao_ya-medium.onnx.json`
- `en_US-amy-medium.onnx`
- `en_US-amy-medium.onnx.json`
- multiple `en_GB-*`, `en_US-*`, `de_DE-*`, `fr_FR-*`, `ar_*`, and other Piper voices

These are old Piper assets only. They must not be executed through `piper.exe`; P4 can use them only if they are converted or proven compatible with `sherpa-onnx` native VITS/Piper loading.

## sherpa-onnx Requirement Check

Official sherpa-onnx Piper/VITS documentation states that converted Piper models need:

- VITS model ONNX
- `tokens.txt`
- `espeak-ng-data`

The plain Piper `.onnx.json` config is not enough for the current `SherpaOnnxTtsEngine` seed, which passes `model_path`, `tokens_path`, and `data_dir` into the sherpa C API.

## Mirror Check

`hf-mirror.com` was tested without local proxy by downloading:

`https://hf-mirror.com/csukuangfj/vits-piper-en_US-amy-low/resolve/main/tokens.txt`

Result: success, 763 bytes.

This suggests converted `csukuangfj/vits-piper-*` model repositories can be downloaded from the domestic Hugging Face mirror later, but large model downloads were not started in P1.

## Downloaded Local Candidate Models

Downloaded without process proxy from `hf-mirror.com`:

- `models/sherpa/vits-piper-en_US-amy-low`
  - Source repo: `csukuangfj/vits-piper-en_US-amy-low`
  - Files downloaded: ONNX, JSON, `tokens.txt`, full `espeak-ng-data`
  - Local file count / size: 362 files, about 81 MB
- `models/sherpa/vits-piper-zh_CN-huayan-x_low`
  - Source repo: `csukuangfj/vits-piper-zh_CN-huayan-x_low`
  - Files downloaded: ONNX, JSON, `tokens.txt`, `MODEL_CARD`
  - Local file count / size: 4 files, about 20 MB
  - Intended to reuse the shared `espeak-ng-data` from the English model directory unless P4 proves a separate copy is required.

These model files are intentionally ignored by Git via `/models/*`.

## Blocked / Unverified

- `sherpa-onnx` Windows native C API package is not downloaded or linked.
- Candidate TTS model files are present locally, but no model has been accepted into the runtime layout yet.
- Old Piper ONNX files are not yet converted to sherpa-compatible metadata/tokens.
