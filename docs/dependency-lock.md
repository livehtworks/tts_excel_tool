# Dependency Lock

Status: partial, P1 in progress.

## Toolchain

| Item | Version / Commit | Source | Status |
|---|---:|---|---|
| Visual Studio | 2022 Community | Local install | Found |
| MSVC | 19.44.35225.0 | VS2022 dev environment | Core build verified |
| Ninja | `D:\programsoft\tools\ninja.exe` | Local install | Core build verified |
| CMake | 4.4.2 | Tsinghua PyPI mirror, build-tool only | Available for vcpkg |
| vcpkg registry | `a2b75031b909a2d6b051725c3909230c72d4bd4a` | `https://gitee.com/mirrors/vcpkg.git` | Cloned |
| vcpkg tool | `2026-07-27-98d7cb0cf1f4686a3e43aa5672b6230c1d56bce8` | Bootstrapped by vcpkg | Available |

## vcpkg Manifest Targets

| Package | Version Intended / Resolved by Dry-Run | Status |
|---|---:|---|
| wxwidgets | 3.2.8.1 | Dry-run resolved; install blocked by system proxy |
| rapidfuzz-cpp | 3.3.3 | Dry-run resolved |
| utf8proc | 2.11.3 | Dry-run resolved |
| openxlsx | 0.5.1 | Dry-run resolved |
| libxlsxwriter | 1.2.4#1 | Dry-run resolved |
| miniaudio | 0.11.25 | Dry-run resolved |

## Native TTS Dependency

`sherpa-onnx` is intentionally not locked here yet. It requires a separate Windows native C API package/build and repeated synthesis stability acceptance in P4.

## Download Policy Notes

- Project commands clear `HTTP_PROXY`, `HTTPS_PROXY`, `ALL_PROXY`, and lower-case variants before downloads.
- Git commands for dependency probing set `GIT_CONFIG_GLOBAL=NUL` to avoid the user's global proxy config.
- vcpkg still auto-detects Windows IE/system proxy settings as `127.0.0.1:7897`; real dependency installation is therefore blocked until that system proxy path is disabled or the user permits it.
