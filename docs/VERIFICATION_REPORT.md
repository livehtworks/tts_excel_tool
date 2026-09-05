# R8 Verification Report

本报告记录当前工作区源码的本地自动化验证事实。R8 target-machine acceptance 仍未执行，因此 `P9_TARGET_MACHINE_ACCEPTANCE_REVOKED` 继续有效。

## 已执行

```powershell
cmd.exe /c 'call "<VS2022>\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul && cmake --build --preset windows-core'
cmd.exe /c 'call "<VS2022>\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul && ctest --preset windows-core --output-on-failure'

cmd.exe /c 'call "<VS2022>\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul && cmake --build --preset windows-release'
cmd.exe /c 'call "<VS2022>\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul && ctest --preset windows-release --output-on-failure'
```

## 结果

- `windows-core` build: passed. Evidence: `logs/r8-windows-core-build-2.log`.
- `windows-core` CTest: 4/4 passed. Evidence: `logs/r8-windows-core-ctest-2.log`.
- `windows-release` build: passed. Evidence: `logs/r8-windows-release-build-4.log`.
- `windows-release` CTest: 7/7 passed. Evidence: `logs/r8-windows-release-ctest-2.log`.

## 本轮覆盖点

- R8 Reference edit raw-row invalidation semantics.
- WorkerQueue construction stress, exception barrier, and stop-token task API.
- OpenXLSX trailing blank-header data retention.
- Compare anchor/alignment threshold invariant, invalid thresholds, GB18030/strict UTF-8 text import, whitespace-only filtering, and memory budget rejection.
- XLSX final write through same-directory atomic replace helper.
- ModelRegistry duplicate-id rejection, package-contained model paths, metadata validation, and bad-model diagnostics.
- Playback stale-request first gate, lifecycle API removal, engine invariant, load-failure active-id clearing, sherpa audio validation, and miniaudio device lifecycle locking.
- Config schema v3 explicit language selection mode, speech-rate persistence hook, and Compare threshold persistence hook.
- Compare UI data-backed table snapshot and shared export snapshot.
- Packaging/source scripts parse successfully and implement fresh staging/source hygiene rules; no package/source ZIP was generated in this verification pass.

## 未完成

- TSAN/ASAN/UBSAN reruns for the final R8 delta were not run in this pass.
- Target Windows PC U01-U08 manual acceptance has not been executed.
- MOSS-TTS-Nano native adapter remains intentionally blocked.
