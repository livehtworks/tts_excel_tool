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
  - LibXlsxWriterExporter (adapter invoked by the background job)
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

业务 worker 固定为 2 条：`BackgroundJobWorker` 和 `PlaybackWorker`。R2 关闭时由 ApplicationRuntime 持有至多一条可 join 的收尾线程，只等待业务 worker、设备、缓存与引擎退出，不接收新业务。MainFrame 的 timer 等待完成后销毁窗口；wxApp 从真实 WM_NCDESTROY 默认处理返回处捕获时间，在 Runtime 日志关闭后顺序追加该诊断。不存在并行运行所有者、线程池、事件总线、actor 或协程框架。

## 4. TTS 生命周期

`TtsService -> ITtsEngine` 是唯一稳定边界。

- 请求音频时先验证完整 voice/config/asset 身份；缓存命中不加载模型、不合成；
- 同一模型加载身份的请求复用 engine，包括同模型不同 speaker ID；speaker ID 仍参与音频缓存身份；
- 模型资源、前端或加载参数导致加载身份改变时显式 unload/load；单纯切换 speaker ID 不重载；
- UI 只构造播放请求，不在 wx 事件处理函数里加载或卸载 TTS 模型；
- 原生加载返回后重新检查取消，再决定是否合成；原生合成返回后取消的请求不播放、不回填缓存；
- 任何 backend 错误直接显示，不做隐藏 fallback；
- sherpa-onnx 的具体 API/version 只允许存在于 `SherpaOnnxTtsEngine`；
- MOSS 推理图的具体 orchestration 只允许存在于 `MossNanoTtsEngine`。

## 5. Unicode

- 文件路径：使用 `std::filesystem::path` 作为 domain/service 边界，不在上层转换成 ANSI `char*`。
- 文本：业务层保存 UTF-8；文本算法开始时 decode 为 code point 序列。
- 正式生产归一化由四个版本化预设及其完整选项决定：None/NFC/NFKC、casefold、空白与标点处理。严格模式按原文判等，CER/WER 和 Indel 使用不同的度量与通过条件。
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
- TTS 模型常驻到加载身份改变或 Runtime 关闭；
- 直接从 TTS 得到 float PCM buffer；
- PCM 直接交 AudioPlayer；
- 启用音频缓存时将有效 PCM 持久化到受管 cache/tts-v1；不是播放临时文件，播放仍直接消费 PCM；
- Excel 分析不得无条件复制整个 workbook 多份；
- 1000 条文本对齐必须在普通 CPU 可用范围，最终用 benchmark 固定门槛。

## 8. 比较界面投影

- CompareService 的不可变报告是网格、行详情和导出的共同来源；查看结果组不修改执行输入或报告集合。
- 行详情使用已有 fragments、度量、状态、S/D/I/N 与来源信息，不重新计算差异。来源编号是导入记录序号，不是物理文件行号；legacy 导入保留原有跳过空记录行为。
- Indel 的编辑统计显示 N/A；归一化判定 OK 不会隐藏原文差异。CER/WER 保留大于 100% 的错误率及 N=0 未定义状态。
- 严格逐行模式禁用但保留归一化选项；严格顺序模式仅将其用于找对应，最终仍按原文判等。
- 参数文本仅验证并更新草稿，失焦或开始时通过 Runtime 串行保存入口提交；路径与组名键入不写配置。高级区折叠只改变布局。
- CompareExecutionContext 统一约束 512MiB 算法分配峰值；多组报告、评分矩阵、DP/trace、原文/归一化和字符差异均计入。每次对比独立取消，不停止整个 BackgroundJobWorker。

工作簿来源和持久化所有权见 `DATA_FACTS.md`；当前实现与验收状态见 `PROJECT_STATUS.md`，历史审核记录不是当前验收结论。

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
