# AdayoCorpusTool C++ Rewrite Seed

这是对旧 `tts_excel_tool` 的**业务保留式重写骨架**，不是 Python→C++ 逐行移植。

## 已固定架构

- C++20 / CMake
- wxWidgets 3.2 stable（Windows 原生 GUI；P1 固定实际依赖版本）
- Core / Service / Adapter 三层边界，不引入 HTTP、本地浏览器、WebView、Python runtime
- sherpa-onnx 原生 C API 作为 Piper/VITS 首个 TTS adapter
- MOSS-TTS-Nano 保留独立 adapter 边界，但在官方 ONNX 推理序列完成 C++ 等价验证前禁止假实现
- 文本比对全程按 Unicode code point，不允许在 UTF-8 byte 上做 Levenshtein/Diff
- Excel 输入与输出解耦：OpenXLSX reader / libxlsxwriter writer
- 最多两条受控串行 WorkerQueue：PlaybackService 专用播放队列，以及桌面 IO/Compare/Export 后台队列；GUI 线程不做模型加载、TTS 推理、批量对齐、Excel 大文件 IO

## 本种子已实现并可独立验证的核心

- UTF-8 安全 decode/encode
- TextNormalizer（无 utf8proc 时保底；正式桌面构建必须在 P1/P6 接入 utf8proc）
- Unicode code point 相似度
- 字符级双边 Diff
- 高置信度锚点 + 顺序约束全局对齐
- 对齐阈值与 OK/NG 阈值分离
- MISSING / EXTRA 状态
- 旧版列语言/类型推断核心规则迁移
- 旧版多行单元格展开、参考列重复、空行过滤、播放列结果列插入
- ITtsEngine / TtsService 边界
- sherpa-onnx VITS/Piper native adapter 初版代码
- wxWidgets 两功能页骨架
- 核心回归测试

## 本地先验收 Core

```bash
cmake -S . -B build -DADAYO_BUILD_DESKTOP=OFF -DADAYO_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

这个步骤不需要 wxWidgets / sherpa-onnx / OpenXLSX / libxlsxwriter。

## Windows 桌面发布构建

共享 `windows-release` preset 不包含个人机器绝对路径。先设置两个环境变量：

```powershell
$env:VCPKG_ROOT = "D:/path/to/vcpkg"
$env:ADAYO_SHERPA_ONNX_ROOT = "D:/path/to/sherpa-onnx-v1.13.6-win-x64-shared-MD-Release-lib"
```

然后构建：

```cmd
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release --output-on-failure
```

`ADAYO_BUILD_DESKTOP=ON` 是正式应用构建，必须同时启用 OpenXLSX、libxlsxwriter、nlohmann-json、utf8proc、rapidfuzz、miniaudio、sherpa-onnx。libxlsxwriter 使用仓库内 `vcpkg-ports/libxlsxwriter` overlay port 修补安装头文件，不需要手工进入 vcpkg buildtree 复制文件。

## 禁止把旧实现带回来

正式重写不得恢复以下路径：

- FastAPI + 浏览器页面
- 2500 行级单文件状态对象
- Python / PyTorch runtime
- PowerShell/System.Speech TTS
- 每句启动 `piper.exe` 子进程
- 每句生成临时 WAV 再用 winsound 播放
- TTS 失败后静默切换另一个 backend
- UI 直接持有模型对象/Excel 对象
- raw UTF-8 `std::string` 字节级文本距离/Diff

完整执行顺序见 `docs/CODEX_EXECUTION_PLAN.md`。
