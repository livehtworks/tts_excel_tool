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

Command:

```cmd
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
cmake -S . -B build-core -G Ninja -DADAYO_BUILD_DESKTOP=OFF -DADAYO_BUILD_TESTS=ON
cmake --build build-core
ctest --test-dir build-core --output-on-failure
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

## Blocked / Unverified

- `windows-release` preset is not yet verified because vcpkg dependencies have not been installed.
- Even after clearing process proxy variables and ignoring Git global proxy config, vcpkg prints `Automatically setting %HTTP(S)_PROXY% environment variables to "127.0.0.1:7897"` from Windows IE/system proxy settings. Per the user request, real vcpkg install was not run through that proxy path.
- wxWidgets empty shell EXE has not yet been launched.
- No dependency lock SHA256 values have been recorded yet for vcpkg-built packages.

## Forbidden-Scope Check

- No Python runtime was added to CMake or release presets.
- No Qt/WebView/Electron/C# stack was introduced.
- No old Python directory is referenced by the new build.
