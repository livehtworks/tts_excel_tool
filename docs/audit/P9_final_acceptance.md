# P9 Final Packaging And Acceptance

Status: locally accepted; target-machine acceptance remains pending.

## Scope

- Build and test the Windows release preset.
- Package only fixed local release artifacts; do not download during packaging.
- Include runtime directories: `logs`, `cache`, `exports`, and `config`.
- Include accepted sherpa/Piper voices only:
  - `vits-piper-en_US-amy-low`
  - `vits-piper-zh_CN-xiao_ya-medium-int8`
- Keep MOSS disabled because P8 is `MOSS_PORT_BLOCKED`.

## Local Acceptance

- Build command:
  - `cmake --build --preset windows-release --config Release`
- Regression command:
  - `ctest --preset windows-release --output-on-failure`
- Result:
  - `7/7 tests passed`
  - Total test time: `122.33 sec`
  - P4 native sherpa TTS stability test: `121.80 sec`
- Package smoke:
  - Started `AdayoCorpusTool.exe` from the independent package directory.
  - Process remained alive after 5 seconds, proving immediate DLL/runtime load succeeded.
  - The process was closed after smoke verification.

## Package Output

- First local package directory:
  - `D:/programcode/python/tts_excel_tool/dist/AdayoCorpusTool-win-x64-20260818-190920`
- First local desktop ZIP:
  - `C:/Users/thevil/Desktop/AdayoCorpusTool-win-x64-20260818-190920.zip`
- ZIP size:
  - `94,507,458 bytes`

Final ZIP should be regenerated after the P9 commit so `PACKAGE_MANIFEST.txt` records the final source commit.

## Not Covered Locally

- A separate clean company target machine was not available in this workspace session.
- MOSS remains disabled by P8 `MOSS_PORT_BLOCKED`.
