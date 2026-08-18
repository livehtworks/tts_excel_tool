# Execution Notes

## Windows / MSVC

- `cl.exe` is not available in a plain PowerShell session on this machine.
- Use VS2022 dev environment before CMake build commands:

```cmd
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
```

- MSVC must compile source as UTF-8. Without `/utf-8`, Chinese and Arabic string literals fail under code page 936.

## Git

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
- Use the fixed vcpkg toolchain path in presets: `D:/programsoft/tools/vcpkg/scripts/buildsystems/vcpkg.cmake`.

## OpenXLSX

- OpenXLSX 0.5.1 only exposes `XLDocument::open(const std::string&)` in the tested package.
- `OpenXlsxWorkbookReader` stages non-ASCII workbook paths through `%TEMP%\adayo_openxlsx` before reading. Keep Chinese path coverage in `adayo_p2_tests`.

## Model Downloads

- `hf-mirror.com` is usable for sherpa/Piper model files without the local proxy.
- Plain old Piper `.onnx + .json` files are not enough for the current sherpa adapter; converted models also need `tokens.txt` and `espeak-ng-data`.
