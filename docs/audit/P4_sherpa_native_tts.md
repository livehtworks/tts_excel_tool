# P4 Sherpa Native TTS Audit

Date: 2026-08-18

## Native Dependency

- sherpa-onnx: `v1.13.6`
- Package: `sherpa-onnx-v1.13.6-win-x64-shared-MD-Release-lib.tar.bz2`
- Source: official k2-fsa/sherpa-onnx GitHub release
- SHA256: `DCA033829D3A7E74C127FC0D349A12257FB890FE5038A381AB1706E4B35CF0FA`
- Local root: `D:/programsoft/tools/sherpa-onnx/sherpa-onnx-v1.13.6-win-x64-shared-MD-Release-lib`
- Header: official `v1.13.6/sherpa-onnx/c-api/c-api.h`
- Header SHA256: `426DB2C6ACFB51E02143AECE67C45779FAE699D961C7C26CCF6F1388FDEAA2DF`

## Models

- English: `models/sherpa/vits-piper-en_US-amy-low`
- Chinese accepted model: `models/sherpa/vits-piper-zh_CN-xiao_ya-medium-int8`
- Chinese model package: `vits-piper-zh_CN-xiao_ya-medium-int8.tar.bz2`
- Chinese model SHA256: `EAB027E194E70289233CF12308373611FA4E2E96EF2D97354EF433AB663D831F`

The earlier `vits-piper-zh_CN-huayan-x_low` local model did not satisfy sherpa native C API generation because it lacked the Chinese lexicon/FST resources required by this pipeline.

## Acceptance

- English voice generated at speeds `0.5`, `1.0`, and `2.0`.
- Chinese voice generated through native sherpa C API.
- English/Chinese model switching completed 20 cycles.
- 500 consecutive English generations completed with the same loaded voice.
- Output sample rate and sample count were checked for every generated buffer.
- Missing model/tokens/data paths are reported explicitly before native load.

## Commands

```cmd
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
cmake --fresh --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release -R adayo_p4_tts_tests --output-on-failure
```

Result:

```text
adayo_p4_tts_tests PASS
500 English sherpa generations elapsed_ms=41395
```

Full release regression after P5:

```text
4/4 tests passed
adayo_p4_tts_tests Passed 150.74 sec
```

## Forbidden-Scope Check

- No `piper.exe` subprocess.
- No PowerShell/System.Speech fallback.
- No hidden backend fallback.
- No per-sentence temp WAV requirement.
