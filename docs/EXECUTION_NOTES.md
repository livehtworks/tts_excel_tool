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

## Audio Cache / Windows IO

- Close the metadata input stream before atomically replacing that metadata file: Windows open handles without delete sharing prevent replacement.
- Cache LRU timestamps must distinguish successive operations within one clock tick; timestamp ties must not evict a more recent entry by hash ordering.
- Cache acceptance uses fresh external roots. Do not run fault/clear/quota tests against the application's established cache or model directory.
- LRU metadata updates reserve bounded temporary space but do not rescan every cache file on every memory hit; quota-changing operations still recount the tree.
- Native tests use a process-unique root, and Chinese filesystem components must use `PathFromUtf8`. A narrow UTF-8 path literal on Windows creates a mojibake directory even with `/utf-8`.
- Windows headers define `small` as a macro; avoid it as a local identifier in native tests.
- Unprivileged symlink creation is unavailable on this workstation. `scripts/new_cache_junction_fixture.ps1` creates an isolated junction for `ADAYO_REVIEW_CACHE_JUNCTION`; P5 checks rejection without touching the target.
- `scripts/test_export_xml.ps1` verifies actual ZIP/XML red runs from the P7 artifact directory. Native OpenXLSX content readback remains a separate assertion.

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
- The historical huayan x_low config used an escaping espeak path and an old converted model without the Piper voice metadata. Preparing its already downloaded original with current upstream metadata and contained espeak data passes native synthesis; it is an espeak voice, not a g2pW voice requiring Xiao Ya's lexicon.
- The accepted Chinese P4 model is official `vits-piper-zh_CN-xiao_ya-medium-int8`, downloaded without process proxy variables.

## Voice Preparation

- Piper JSON phoneme maps contain distinct uppercase/lowercase keys. PowerShell must use `ConvertFrom-Json -AsHashtable`; ordinary object conversion fails on `X`/`x`. Stop on parsing errors before counting resources.
- Count model/quality variants, named voice datasets, base languages, locales and speaker slots separately. Speaker slots across qualities/corpora are not a deduplicated human-voice count.
- Build target names are `adayo_p2_tests` and `adayo_p4_tts_tests`, not the source filenames. Native voice probes use the production adapter without initializing an audio cache and write diagnostics only to per-run disposable roots.
- `python` currently resolves to CPython 3.14; ONNX 1.22.0 was installed from the Tsinghua PyPI mirror for development-only asset preparation. Production remains C++.
- Newer Piper phoneme maps may contain unused multi-codepoint vowel IDs. Upstream espeak emits NFD codepoints unless `vowel_clusters` is configured. Only those unreachable entries may be omitted from sherpa's character token table; configured clusters and multi-ID mappings must fail explicitly, not be truncated or guessed. Pinyin syllable tokens must remain intact.
- `hebrew` requires Nakdimon/G2P and `japanese` requires OpenJTalk/pitch processing. These are not ordinary espeak voices and must not be labeled as such to pass a loader check. Piper `text` uses explicit NFD normalization (existing utf8proc) plus Sherpa's character frontend and original BOS/PAD/EOS IDs; it must not route through espeak.
- Resource preparation publishes only after candidate synthesis and deployed-path synthesis succeed. Retain metadata journals and prior config snapshots. Weight/data hard links avoid duplicate physical resource sets and preserve the existing voice-root containment contract.
- Native CLI probes must use `wmain` and explicit wide-to-UTF-8 conversion on Windows; narrow `main` arguments lose characters such as the Portuguese voice name's accent under a Chinese system code page.
- espeak initializes process-global data once. A Unicode-path test run after an ASCII model may appear to pass while using the old data directory. The P4 gate now initializes a Unicode root first. Both desktop and native probe embed `activeCodePage=UTF-8` so narrow third-party file I/O uses UTF-8 (Windows 10 1903 or newer); the Portuguese deployed-path probe also runs in a fresh process.

## MOSS-TTS-Nano

- Official source checkouts for P8 assessment are local read-only tooling references under the developer's external tools directory; set the path explicitly outside the repository when repeating that assessment.
- MOSS ONNX uses multiple coordinated graphs plus manifest metadata and SentencePiece tokenization. Do not enable `MossNanoTtsEngine` until C++ tokenizer parity and golden ONNX output checks exist.

## Packaging

- Use `scripts/package_windows.ps1` after building the `windows-release` preset.
- The September review uses create-only outputs under `dist/review-<HEAD>-<run_id>/AdayoCorpusTool`. Existing targets are rejected; the established runtime directory and its model/config/export/cache data are protected.
- Packaging does not create a ZIP unless `-Zip` is passed; the user packages ZIPs manually during development.
- The canonical model source remains `dist/AdayoCorpusTool/model`. Isolated review packages copy only manifest-listed models with source/destination hash verification; the script does not download or change the source models.
- UCRT `api-ms-win-*` imports can be loader contracts without physical sibling DLLs. Packaging resolves only API-set names through the System32-only Windows loader and verifies the resulting module path; an arbitrary or nonexistent name is never accepted by prefix alone.

## wxWidgets And Acceptance Timing

- A shrinking status label needs both a zero minimum width and `wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END`. `SetLabel` otherwise expands the native control over adjacent buttons even when the sizer has a bounded width.
- Comparison grid labels use separate group/metric lines and a metric-width column so metric units remain readable. Grid cell overflow is disabled in mapping/runtime/compare views.
- Track playback state transitions when restoring idle text; continuously overwriting idle status would hide export/error messages, while never updating idle leaves stale Stopping text.
- Probe first-ready timing starts before model scan and cache initialization. It excludes OS process creation/DLL loader time. WASAPI probe callback timing also includes recording setup and its pre-roll; do not report it as GUI click-to-audible latency.
- Save complete pre-task inventories when whole-tree preservation is an acceptance assertion. Post-task hashes cannot reconstruct a missing pre-task baseline.
