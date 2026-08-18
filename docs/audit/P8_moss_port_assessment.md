# P8 MOSS-TTS-Nano Native Port Assessment

Status: `MOSS_PORT_BLOCKED`.

## Sources Reviewed

- Official source: `https://github.com/OpenMOSS/MOSS-TTS-Nano`
- Official reader/integration source: `https://github.com/OpenMOSS/MOSS-TTS-Nano-Reader`
- ONNX model description: `https://huggingface.co/OpenMOSS-Team/MOSS-TTS-Nano-100M-ONNX`
- Local read-only checkouts:
  - `D:/programsoft/tools/moss-tts/MOSS-TTS-Nano`
  - `D:/programsoft/tools/moss-tts/MOSS-TTS-Nano-Reader`

## Confirmed Inference Shape

The official ONNX path is not a single `Session::Run` TTS graph. It is a coordinated runtime around:

- SentencePiece text tokenizer and text normalization;
- `moss_tts_prefill.onnx`;
- `moss_tts_decode_step.onnx` with KV cache handoff;
- `moss_tts_local_fixed_sampled_frame.onnx` or related local decoder graphs;
- audio-token frame accumulation;
- `moss_audio_tokenizer_decode_full.onnx` or streaming codec decode;
- manifest/meta JSON files that define tensor names, token ids, prompt templates, voices, generation defaults, and external weight data.

The official Android example proves a minimal product-side ONNX route, but it intentionally accepts pre-tokenized text ids and therefore does not satisfy this desktop tool's arbitrary multilingual text input requirement by itself.

## Decision

Do not enable `MossNanoTtsEngine` in the formal runtime UI yet.

Reasons:

- The C++ adapter has no verified SentencePiece/tokenization implementation for arbitrary Chinese/English/Japanese/German input.
- No local golden set has been generated and checked against the official Python/ONNX reference.
- Random sampling and decode-cache semantics must be matched before exposing the engine beside sherpa.
- The release package must remain Python/PyTorch-free.

## Guardrail Added

`adayo_moss_adapter` is now compiled and `adayo_p8_moss_blocked_tests` asserts that `MossNanoTtsEngine` remains explicit-fail until the official sequence is ported and verified.

## Required Follow-Up For Real P8 Completion

- Add a native C++ SentencePiece dependency and verify tokenizer parity.
- Download official ONNX TTS and audio-tokenizer assets into an ignored local model directory.
- Generate small offline golden data from official `infer_onnx.py`.
- Port manifest parsing, prefill, decode-step KV cache, fixed/full sampling, and codec decode into `src/adapters/tts/moss/*`.
- Prove Chinese, English, Japanese, and German fixed texts on CPU before marking the engine Ready.
