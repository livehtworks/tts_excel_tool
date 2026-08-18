# Dependency Lock

Status: P1 dependency baseline verified for current Windows machine.

## Toolchain

| Item | Version / Commit | Source | Status |
|---|---:|---|---|
| Visual Studio | 2022 Community | Local install | Found |
| MSVC | 19.44.35225.0 | VS2022 dev environment | Core/release build verified |
| Ninja | `D:\programsoft\tools\ninja.exe` | Local install | Core/release build verified |
| CMake | 4.4.2 | Tsinghua PyPI mirror, build-tool only | Available for vcpkg |
| vcpkg registry | `a2b75031b909a2d6b051725c3909230c72d4bd4a` | `https://gitee.com/mirrors/vcpkg.git` | Cloned |
| vcpkg tool | `2026-07-27-98d7cb0cf1f4686a3e43aa5672b6230c1d56bce8` | Bootstrapped by vcpkg | Available |
| vcpkg toolchain | `D:/programsoft/tools/vcpkg/scripts/buildsystems/vcpkg.cmake` | Local fixed path | Release build verified |

## vcpkg Manifest Targets

| Package | Version Intended / Resolved by Dry-Run | Status |
|---|---:|---|
| wxwidgets | 3.2.8.1 | Installed and linked |
| rapidfuzz-cpp | 3.3.3 | Installed and linked in release preset |
| utf8proc | 2.11.3 | Installed and linked in release preset |
| nlohmann-json | 3.12.0#2 | Installed and linked through `adayo_persistence` |
| openxlsx | 0.5.1 | Installed and linked through `adayo_excel_reader` |
| libxlsxwriter | 1.2.4#1 | Installed; adapter not enabled yet |
| miniaudio | 0.11.25 | Installed; adapter not enabled yet |

Archive SHA256 details are recorded in:

`docs/audit/P1_vcpkg_download_hashes.tsv`

## Release Build Verification

```cmd
cmake --fresh --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

Result after P2: `2/2 adayo_core_tests, adayo_p2_tests PASS`.

Generated EXE:

`build/windows-release/AdayoCorpusTool.exe`

App-local DLL smoke includes wxWidgets and utf8proc DLLs copied beside the EXE.

## Native TTS Dependency

`sherpa-onnx` is intentionally not locked here yet. It requires a separate Windows native C API package/build and repeated synthesis stability acceptance in P4.

## Download Policy Notes

- Initial project commands cleared `HTTP_PROXY`, `HTTPS_PROXY`, `ALL_PROXY`, and lower-case variants before downloads.
- Git commands for dependency probing set `GIT_CONFIG_GLOBAL=NUL` to avoid the user's global proxy config.
- vcpkg auto-detects Windows IE/system proxy settings as `127.0.0.1:7897`.
- User later explicitly allowed proxy use for dependency installation. Voice/model downloads remain no-proxy/domestic-mirror by request.
