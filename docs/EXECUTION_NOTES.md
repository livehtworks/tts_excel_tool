# Execution Notes

## Windows / MSVC

- `cl.exe` is not available in a plain PowerShell session on this machine.
- Use VS2022 dev environment before CMake build commands:

```cmd
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
```

- MSVC must compile source as UTF-8. Without `/utf-8`, Chinese and Arabic string literals fail under code page 936.

## Git

- The source remote is `origin` at `https://github.com/livehtworks/tts_excel_tool.git`, with `main` as the publication branch. Use the existing GitHub CLI / Git credential-manager authentication; never embed credentials in repository URLs or files.
- Before pushing, inspect the staged payload. Keep `dist/` (including the canonical runtime model directory), generated outputs, `backup/`, and `CMakeUserPresets.json` excluded. A source push does not require building or packaging the application.
- `backup/` contains the archived old project and is intentionally ignored.
- `build-*` and `build/` directories are generated and ignored.

## Fixture Generation

- `openpyxl` was installed from the Tsinghua PyPI mirror only as a development-time helper to create a desensitized P0 `.xlsx` fixture.
- Python is not part of the new C++ runtime, build chain, or release package.

## vcpkg / Network

- vcpkg was cloned from `https://gitee.com/mirrors/vcpkg.git`.
- vcpkg package dry-run resolves the intended manifest, including `wxwidgets@3.2.8.1`.
- Enabling OpenXLSX pulls a large Boost header/component chain through vcpkg; initial install can run for several minutes.
- Clear proxy variables before dependency commands:

```powershell
$env:HTTP_PROXY=$null; $env:HTTPS_PROXY=$null; $env:ALL_PROXY=$null; $env:NO_PROXY=$null
$env:http_proxy=$null; $env:https_proxy=$null; $env:all_proxy=$null; $env:no_proxy=$null
$env:GIT_CONFIG_GLOBAL='NUL'
$env:VCPKG_FORCE_SYSTEM_BINARIES='1'
```

- vcpkg reads Windows IE/system proxy settings and auto-sets `HTTP(S)_PROXY` to `127.0.0.1:7897`.
- The user explicitly allowed proxy usage for C++ dependency installation. Keep model/voice downloads on domestic mirrors and avoid proxy for large model files.
- Shared presets read `VCPKG_ROOT` and `ADAYO_SHERPA_ONNX_ROOT` from the environment. This machine's local values live only in ignored `CMakeUserPresets.json`.

## OpenXLSX

- OpenXLSX 0.5.1 only exposes `XLDocument::open(const std::string&)` in the tested package.
- `OpenXlsxWorkbookReader` converts the native `std::filesystem::path` to UTF-8 only at the OpenXLSX API boundary and opens the requested workbook directly. Do not reintroduce `%TEMP%` staging for Unicode paths.

## libxlsxwriter

- The repository provides `vcpkg-ports/libxlsxwriter` as an overlay port for libxlsxwriter 1.2.4. It automatically installs the `include/third_party` headers required by `xlsxwriter/common.h`.
- `LibXlsxWriterExporter` writes via libxlsxwriter's memory output buffer and then persists bytes with native filesystem IO. Do not route Unicode output filenames through temporary ASCII paths.

## Model Downloads

- Release/runtime model resources now live under `dist/AdayoCorpusTool/model`; do not keep a second runtime model copy under workspace `models/`.
- Use ModelScope for bulk model/voice downloads without the local proxy:

```powershell
.\scripts\download_model_resources.ps1 -MaxWorkers 8
```

- `modelscope.cn/models/rhasspy/piper-voices` is the domestic source for Piper voices. The high-level ModelScope snapshot API currently truncates the recursive listing at 3000 files, so `scripts/download_piper_voices_modelscope.py` lists Piper voice files per language root and downloads explicit paths.
- Piper full voice assets currently validated under `dist/AdayoCorpusTool/model/piper/voices`: 348 files, 174 `.onnx` voices, 50 language directories, 10.688 GiB / 11.476 GB.
- MOSS-TTS-Nano ONNX resources currently validated under `dist/AdayoCorpusTool/model/moss`: `MOSS-TTS-Nano-100M-ONNX` and `MOSS-Audio-Tokenizer-Nano-ONNX`, 22 files, 0.710 GiB / 0.763 GB combined.
- `hf-mirror.com` is usable for small model metadata downloads without the local proxy, but it was too slow for Piper full voice downloads on this workstation.
- Plain old Piper `.onnx + .json` files are not enough for the current sherpa adapter; converted models also need `tokens.txt` and `espeak-ng-data`.
- The local huayan x_low Chinese model failed native sherpa generation because Chinese lexicon/FST resources were missing.
- The accepted Chinese P4 model is official `vits-piper-zh_CN-xiao_ya-medium-int8`, downloaded without process proxy variables.

## MOSS-TTS-Nano

- Official source checkouts for P8 assessment are local read-only tooling references under the developer's external tools directory; set the path explicitly outside the repository when repeating that assessment.
- MOSS ONNX uses multiple coordinated graphs plus manifest metadata and SentencePiece tokenization. Do not enable `MossNanoTtsEngine` until C++ tokenizer parity and golden ONNX output checks exist.

## Packaging

- Use `scripts/package_windows.ps1` after building the `windows-release` preset.
- The release directory is fixed at `dist/AdayoCorpusTool` and is updated in place.
- Packaging does not create a ZIP unless `-Zip` is passed; the user packages ZIPs manually during development.
- Runtime model resources are validated in `dist/AdayoCorpusTool/model`. The packaging script does not download models and does not copy a separate model tree from workspace `models/`.
