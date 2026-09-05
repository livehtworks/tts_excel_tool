# 新架构冻结说明

## 1. 主架构

```text
wxWidgets UI (Windows GUI thread)
        |
        v
ApplicationRuntime
  - FileLogger
  - BackgroundJob WorkerQueue
  - WorkbookService / ModelRegistry
  - TtsService / MiniaudioPlayer / PlaybackService
        |
        v
Application Services
  - WorkbookService
  - TtsService
  - PlaybackService
  - CompareService
  - ExportService
        |
        v
Pure C++ Core
  - ColumnAnalyzer / ViewBuilder
  - TextNormalizer
  - TextSimilarity
  - SequenceAligner
  - CharacterDiff
  - WorkerQueue
        |
        v
Adapters
  - OpenXlsxWorkbookReader
  - LibXlsxWriterExporter
  - SherpaOnnxTtsEngine
  - MossNanoTtsEngine
  - MiniaudioPlayer
  - JsonConfigStore
```

## 2. 依赖方向（禁止反向）

- Core 不 include wxWidgets / sherpa-onnx / ONNX Runtime / Excel 库 / miniaudio。
- Services 只依赖 Core 接口和 domain types。
- Adapter 实现 Core/Service 所需接口。
- UI 只调 Service，不直接管理 TTS handle、workbook handle、audio device。
- 配置持久化不允许散落到 UI event handler。

## 3. 线程模型

```text
wxWidgets GUI Thread
│
├── UI only
│   ├── wx controls
│   ├── validation
│   └── immutable request snapshots
│
├── ApplicationRuntime
│   ├── FileLogger
│   ├── WorkbookService
│   ├── ModelRegistry
│   ├── TtsService
│   ├── MiniaudioPlayer
│   ├── PlaybackService
│   │   └── PlaybackWorker
│   └── BackgroundJobWorker
│
├── TtsPanel
│   ├── CorpusMappingPanel
│   └── CorpusRunPanel
│
└── ComparePanel

BackgroundJobWorker
   |-- Excel Sheet read/analyze
   |-- text import/compare
   `-- XLSX export

PlaybackWorker
   |-- TTS model ensure/load/switch
   |-- TTS synthesis
   |-- sequence timing
   `-- playback state machine

Audio device callback/thread由 miniaudio 自身管理，PlaybackService 持有播放状态。
Worker 完成后用 wxThreadEvent / CallAfter 回 UI。
```

项目自建 worker 固定为 2 条：`BackgroundJobWorker` 和 `PlaybackWorker`。第一版不引入线程池、事件总线、actor、协程框架。

## 4. TTS 生命周期

`TtsService -> ITtsEngine` 是唯一稳定边界。

- 选择 voice 时加载一次模型；
- 同一 voice 连续合成默认复用 engine；
- 切换 voice 时显式 unload/load；
- UI 只构造播放请求，不在 wx 事件处理函数里加载或卸载 TTS 模型；
- 任何 backend 错误直接显示，不做隐藏 fallback；
- sherpa-onnx 的具体 API/version 只允许存在于 `SherpaOnnxTtsEngine`；
- MOSS 推理图的具体 orchestration 只允许存在于 `MossNanoTtsEngine`。

## 5. Unicode

- 文件路径：使用 `std::filesystem::path` 作为 domain/service 边界，不在上层转换成 ANSI `char*`。
- 文本：业务层保存 UTF-8；文本算法开始时 decode 为 code point 序列。
- 正式生产归一化：utf8proc NFKC + casefold（按配置）+ 空白处理 + 可选标点忽略。
- OpenXLSX 直接打开请求的 Unicode workbook path；libxlsxwriter 通过 memory output buffer + native filesystem write 输出到请求路径；不得重新引入临时 ASCII staging。
- sherpa-onnx v1.13.6 的模型路径必须通过中文目录硬门禁后才能宣称支持。

## 6. Excel

输入和输出故意不用同一个库绑定：

- OpenXLSX：读 XLSX / Sheet / cell value；
- libxlsxwriter：写 XLSX，尤其用于 rich string 字符差异标红。

Core 永远只接收 `WorksheetData` / `RuntimeView` / `CompareRow`。

## 7. 性能边界

旧版性能问题主要由运行时路径叠加引起：HTTP/browser、本地 Python 解释器、大依赖、逐句进程启动、临时 WAV、阻塞播放。

新链要求：

- 一次进程启动；
- TTS 模型常驻到切换 voice；
- 直接从 TTS 得到 float PCM buffer；
- PCM 直接交 AudioPlayer；
- 不为“播放一条”写磁盘；只有用户点击保存 WAV 才落盘；
- Excel 分析不得无条件复制整个 workbook 多份；
- 1000 条文本对齐必须在普通 CPU 可用范围，最终用 benchmark 固定门槛。

## 8. 不允许擅自扩展

第一版禁止：

- 数据库；
- 网络服务；
- 用户系统；
- 自动更新；
- 插件系统；
- Electron/WebView；
- Python bridge；
- C# helper；
- Qt；
- ASR；
- 自动化测试框架接入；
- 云 TTS。
