# R5 Config, Build, and Packaging Remediation

Date: 2026-08-18

## Scope

- Added safe JSON config loading and saving:
  - corrupt config files are renamed to `.corrupt-*` before defaults are used;
  - future schema files are loaded as protected and cannot be overwritten by the UI;
  - saves use same-directory temp files and atomic replacement.
- Replaced shared preset absolute paths with `VCPKG_ROOT` and `ADAYO_SHERPA_ONNX_ROOT`.
- Added desktop configure-time fatal checks for required release adapters.
- Added `vcpkg-ports/libxlsxwriter` overlay port and `vcpkg-configuration.json`.
- Added `models/package-manifest.json` and manifest-driven model validation/copying in `scripts/package_windows.ps1`.
- Completed the missed R4 text import requirement for UTF-16LE/BE BOM input.

## Verification

- `cmake --fresh --preset windows-release-local`
  - rebuilt `libxlsxwriter:x64-windows@1.2.4#2` from the repository overlay port.
- `cmake --build --preset windows-release-local --config Release`
- Shared preset with only environment variables:
  - `VCPKG_ROOT`
  - `ADAYO_SHERPA_ONNX_ROOT`
  - `cmake --preset windows-release`
  - `cmake --build --preset windows-release --config Release`
- Negative desktop dependency configure:
  - `cmake -S . -B build/r5-bad-desktop -G Ninja -DADAYO_BUILD_DESKTOP=ON -DADAYO_BUILD_TESTS=OFF`
  - failed with the expected missing required option error.
- `ctest --preset windows-release -R "adayo_p2_tests|adayo_p6_compare_tests|adayo_p7_export_tests" --output-on-failure`
  - 3/3 passed.
- `scripts/package_windows.ps1 -NoZip -PackageName R5-package-smoke`
  - succeeded, produced 417 files and copied only manifest-listed sherpa models.

## Notes

- `CMakeUserPresets.json` contains this workstation's local paths and is ignored by git.
- The package script still never downloads models.
