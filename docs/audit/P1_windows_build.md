# P1 Windows Build Audit

Stage: P1
Date: 2026-08-18

## Target Baseline

- OS: Windows, local workspace path `D:\programcode\python\tts_excel_tool`
- Compiler: MSVC from Visual Studio 2022 Community
- Generator: Ninja
- Package manager: vcpkg manifest mode
- Desktop UI: wxWidgets 3.2 stable line

## Verified So Far

- CMake available: `cmake version 3.31.6-msvc6`
- CMake 4.4.2 build tool installed from Tsinghua PyPI mirror for vcpkg tool compatibility.
- Ninja available: `D:\programsoft\tools\ninja.exe`
- VS dev environment script found: `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat`
- MSVC compiler during configure: `MSVC 19.44.35225.0`
- Core configure/build/test passes after adding MSVC `/utf-8`.
- `CMakePresets.json` `windows-core` configure/build/test passes.
- `CMakePresets.json` `windows-release` configure/build/test passes with the fixed vcpkg toolchain path `D:/programsoft/tools/vcpkg/scripts/buildsystems/vcpkg.cmake`.
- wxWidgets found by CMake: `3.2.8.1`.
- `AdayoCorpusTool.exe` was generated at `build/windows-release/AdayoCorpusTool.exe`.
- GUI smoke: release EXE started and stayed alive for 5 seconds.
- Chinese path smoke: copied release EXE + app-local DLLs to `build/中文路径启动验证`; EXE started and stayed alive for 5 seconds.
- Downloaded vcpkg source/tool archive SHA256 list: `docs/audit/P1_vcpkg_download_hashes.tsv`.

Command:

```cmd
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
cmake -S . -B build-core -G Ninja -DADAYO_BUILD_DESKTOP=OFF -DADAYO_BUILD_TESTS=ON
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

Result: `1/1 Test #1: adayo_core_tests Passed`.

Windows release command:

```cmd
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
cmake --fresh --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

Result: `1/1 Test #1: adayo_core_tests Passed`.

## Dependency Plan

- `vcpkg.json` tracks: `wxwidgets`, `rapidfuzz-cpp`, `utf8proc`, `openxlsx`, `libxlsxwriter`, `miniaudio`.
- `wxwidgets` is overridden to `3.2.8.1`; current vcpkg default `3.3.3` is not accepted for this project.
- vcpkg registry was cloned from `https://gitee.com/mirrors/vcpkg.git`.
- vcpkg registry HEAD used for manifest baseline: `a2b75031b909a2d6b051725c3909230c72d4bd4a`.
- `sherpa-onnx` remains outside vcpkg until a concrete Windows native release or build commit is pinned and verified.

Dry-run command:

```powershell
$env:HTTP_PROXY=$null; $env:HTTPS_PROXY=$null; $env:ALL_PROXY=$null; $env:NO_PROXY=$null
$env:http_proxy=$null; $env:https_proxy=$null; $env:all_proxy=$null; $env:no_proxy=$null
$env:GIT_CONFIG_GLOBAL='NUL'
$env:VCPKG_FORCE_SYSTEM_BINARIES='1'
.\vcpkg.exe install --triplet x64-windows --x-manifest-root=D:\programcode\python\tts_excel_tool --dry-run
```

Dry-run result: dependency graph resolves and includes `wxwidgets[core,debug-support,sound]:x64-windows@3.2.8.1`.

Real install/build result: dependency installation completed, then `windows-release` built successfully. The user explicitly allowed proxy use for C++ dependencies after the initial no-proxy attempt was blocked by vcpkg/system proxy behavior.

## Blocked / Unverified

- SHA256 is recorded for downloaded vcpkg source/tool archives present in the local caches, excluding the known partial `cmake-4.4.0-windows-x86_64.zip` from an abandoned manual download attempt.
- Release bundle packaging is not complete; P9 still owns final install/package layout.
- OpenXLSX/libxlsxwriter/miniaudio are installed but their adapters are not fully implemented or enabled in this release preset yet.
- sherpa-onnx is not installed or linked yet.

## Forbidden-Scope Check

- No Python runtime was added to CMake or release presets.
- No Qt/WebView/Electron/C# stack was introduced.
- No old Python directory is referenced by the new build.
