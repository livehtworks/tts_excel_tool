# Codex 执行方案：Adayo 语料测试工具 C++ 重写

> 输入工程：本 ZIP 根目录。旧 Python 工程仅作为业务行为证据，不允许逐行翻译或保留运行时。
>
> 总原则：按 P0→P9 顺序执行。每一阶段验收通过并提交后才进入下一阶段。任何“临时回退到 Python/PowerShell/浏览器”都视为失败，不允许为了先跑通而引入。

---

## 全局禁止修改范围

1. 不改主技术栈：C++20 + CMake + wxWidgets 原生 GUI。
2. 不引入 Python、Qt、C#、Electron、WebView、本地 HTTP server。
3. 不把 Core 算法搬进 UI event handler。
4. 不删除 `ITtsEngine`、Excel reader/writer adapter 边界。
5. 不把 MOSS 的 Python 官方示例直接作为子进程调用。
6. 不引入隐藏 TTS fallback；backend 失败必须显式报错并停止当前任务。
7. 不把 `std::string` UTF-8 bytes 直接当字符做距离/Diff。
8. 不删除旧业务基线中的功能来换取开发速度。
9. 不擅自新增数据库、ASR、网络服务或自动化系统接入。
10. 旧 Python 目录不得成为新 EXE 的运行依赖。

---

# P0 — 冻结旧版业务基线与验收样本

**问题现象**  
旧版业务散落在 README、2500 行 `app.py` 与前端 JS 中。直接重写容易漏掉表头行、列映射、多行展开、结果标记、顺序播放等细节。

**根因 / 待验证原因**  
当前 seed 已人工抽取 `docs/BUSINESS_BASELINE.md`，但还缺“真实旧 Excel 样本”的自动化 fixture 和旧版截图/行为证据。

**唯一指定的修改方法**  
只补充业务 fixture 与验收记录：从用户现有真实 Excel 样本复制一份脱敏 fixture；按 `BUSINESS_BASELINE.md` 逐项记录输入→期望输出。不得修改架构和算法。

**涉及文件 / 模块 / 类 / 函数**  
- `docs/BUSINESS_BASELINE.md`
- `tests/fixtures/*`
- 新增 `docs/audit/P0_business_baseline.md`

**禁止修改范围**  
- 不改 Core 代码；
- 不改旧 Excel 原始文件；
- 不添加新业务。

**验收断言**  
- 至少包含：多 Sheet、非第1行表头、多语言表头、多行播放单元格、空单元格、1参考+多播放列、旧结果列。
- 每条基线都有可复现期望值。

**失败后的安全停止条件**  
无法确认某一旧行为时，标记 `UNVERIFIED_OLD_BEHAVIOR` 并停止该行为迁移，不自行猜。

**推荐模型**  
GPT-5.5。主要是代码/业务对照和 fixture 整理，不需要最高推理规格。

---

# P1 — Windows 构建与依赖基线锁定

**问题现象**  
当前 seed 的 Core 可独立编译；桌面依赖尚未在目标公司 Windows PC 上固定。wxWidgets stable 与包管理器最新开发分支可能不同，直接浮动依赖会破坏长期维护。

**根因 / 待验证原因**  
需要在 VS2022 x64 / Windows 10或11 实机验证具体版本组合、CMake target 名、动态/静态发布文件。

**唯一指定的修改方法**  
建立唯一 Windows 构建链：VS2022 + CMake Presets + vcpkg manifest/overlay；固定并记录实际验证 commit/baseline。wxWidgets 固定 3.2 stable 线；OpenXLSX、rapidfuzz-cpp、utf8proc、libxlsxwriter、miniaudio 固定到一次通过的版本。sherpa-onnx 单独作为 pinned native dependency，不用“每次构建下载 latest”。

**涉及文件 / 模块 / 类 / 函数**  
- `CMakePresets.json`
- `vcpkg.json`
- 必要时 `ports/wxwidgets-stable/*`
- `cmake/Dependencies.cmake`
- `docs/audit/P1_windows_build.md`

**禁止修改范围**  
- 不切换 WinUI/Qt/WebView；
- 不把依赖 DLL 复制逻辑散落在源码；
- 不使用 floating master/latest 作为正式 release 基线。

**验收断言**  
- `cmake --preset windows-release` 成功；
- `cmake --build --preset windows-release` 成功；
- Core tests 全过；
- wxWidgets 空壳 EXE 在无开发环境的新目录可启动；
- 中文工程路径和中文 EXE 所在目录均验证一次；
- 生成 `dependency-lock.md` 记录版本、commit、SHA256/来源。

**失败后的安全停止条件**  
任何依赖只能通过“换成另一个 GUI/另一个语言”才能通过时立即停止，保留日志给 GPT review，不擅自换栈。

**推荐模型**  
GPT-5.6 Sol。依赖 ABI、CMake、Windows Unicode 和发布链需要较强跨模块审查。

---

# P2 — XLSX 输入、Sheet/表头行、列分析与配置回填

**问题现象**  
`OpenXlsxWorkbookReader::ReadSheet` 当前故意未实现；旧版用 openpyxl 一次性读表并进行列分析。

**根因 / 待验证原因**  
OpenXLSX 0.5.x API 与 Windows Unicode 路径必须在 P1 锁定版本后实现，避免照搬过期 API。

**唯一指定的修改方法**  
实现 `IWorkbookReader`：读取 Sheet names、指定 header row、将单元格统一转换为显示字符串；数据只落到 `WorksheetData`。随后调用现有 `ColumnAnalyzer`。新增 `JsonConfigStore`，配置键固定为 workbook identity + sheet + header row，完成列映射回填。

**涉及文件 / 模块 / 类 / 函数**  
- `src/adapters/excel/OpenXlsxWorkbookReader.*`
- 新增 `src/services/WorkbookService.*`
- 新增 `src/persistence/JsonConfigStore.*`
- `src/core/workbook/ColumnAnalyzer.*`
- `src/persistence/ConfigModel.h`

**禁止修改范围**  
- 不让 UI 直接持有 `OpenXLSX::XLDocument`；
- 不在 Core include OpenXLSX；
- 不把所有 cell 强制转 ANSI；
- 不为了中文路径失败要求用户改英文路径。

**验收断言**  
- 旧 fixture 的 sheet list、header row、headers、row count 与旧版一致；
- 有效列、5条样本、类型猜测、语言猜测与基线一致或在明确修复项中更正；
- 中文/日文/阿语内容无乱码；
- 中文目录+中文 xlsx 文件名可打开；
- 同 workbook+sheet+header row 可恢复上次 mapping；不同 header row 不串配置。

**失败后的安全停止条件**  
第三方库确实无法处理 Unicode 文件路径时，只允许在 Excel adapter 内使用 hash 命名的 ASCII staging copy；原始路径仍使用 `std::filesystem::path`，且复制失败必须报错。不得要求业务层改路径编码。

**推荐模型**  
GPT-5.5。API 接线明确；遇到 Unicode/库版本差异时切 GPT-5.6 Sol review。

---

# P3 — 运行视图与旧版语料表业务完整迁移

**问题现象**  
Core 已迁移多行展开和结果列插入，但 wxWidgets 还没有真实运行表格、列映射页、编辑回写。

**根因 / 待验证原因**  
旧版 UI/状态耦合，需要在新架构里把“源工作表数据”和“显示展开行”分开。

**唯一指定的修改方法**  
新增 `CorpusMappingPanel` + `CorpusRunPanel`；UI 只操作 `WorkbookService`/`CorpusViewService`。复用 `ViewBuilder` 生成 display rows，并用 `DisplayRowMeta` 实现编辑回写。结果状态单独保存在 runtime session，不写进源数据列。

**涉及文件 / 模块 / 类 / 函数**  
- `src/ui/TtsPanel.*`（拆成容器）
- 新增 `src/ui/CorpusMappingPanel.*`
- 新增 `src/ui/CorpusRunPanel.*`
- `src/core/workbook/ViewBuilder.*`
- 新增 `src/services/CorpusViewService.*`

**禁止修改范围**  
- 不删除多行展开；
- 不允许多参考列；
- 不允许 0 播放列进入运行视图；
- 不直接修改 Excel 文件作为“编辑保存”；运行时编辑只修改 session data，导出时写新文件。

**验收断言**  
- 旧 fixture 运行视图行数/列顺序与旧版一致；
- 参考列位于播放列前；
- 每个播放列后紧跟结果列；
- 多行 cell 展开与旧版一致；
- 编辑任一展开句只修改对应 source segment，不污染同源其它 segment；
- 结果循环 `blank→OK→NG→blank`；
- 大表滚动不因把整个表反复 rebuild 导致明显卡顿。

**失败后的安全停止条件**  
如果编辑回写无法唯一定位到 raw row + source col + segment index，禁止保存该编辑并显示错误；不能猜测写回位置。

**推荐模型**  
GPT-5.5。GUI 与状态映射工作量大但逻辑明确。

---

# P4 — sherpa-onnx + Piper/VITS 原生 CPU TTS

**问题现象**  
旧版每句通过 `piper.exe` 子进程/临时 WAV/系统播放，开销高且错误被 fallback 掩盖。Seed 已提供 `SherpaOnnxTtsEngine` 初版 native C API adapter，但未在 Windows 目标机编译验证。

**根因 / 待验证原因**  
需要固定 sherpa-onnx 版本、Windows native library 和 VITS/Piper model metadata；另外必须验证同一 TTS 实例连续多次生成的稳定性，而不是只测一条。

**唯一指定的修改方法**  
完成并只使用 `SherpaOnnxTtsEngine`：模型选择后 native load；每句直接返回 float PCM `AudioBuffer`；同 voice 默认复用 engine；切 voice 显式 unload/load。建立 `ModelRegistry` 扫描 `models/sherpa/*/model.json`，模型路径不硬编码进 UI。

**涉及文件 / 模块 / 类 / 函数**  
- `src/adapters/tts/SherpaOnnxTtsEngine.*`
- `src/core/tts/ITtsEngine.h`
- `src/services/TtsService.*`
- 新增 `src/services/ModelRegistry.*`
- `models/README.md`

**禁止修改范围**  
- 禁止调用 `piper.exe`；
- 禁止 PowerShell/System.Speech；
- 禁止每句写 temp WAV 才能播放；
- 禁止 TTS 失败后自动换其它引擎；
- 禁止 UI 保存 sherpa native pointer。

**验收断言**  
- 至少中/英各一个 Piper/VITS voice native 生成成功；
- 连续 500 句同 voice 生成无 crash、无 handle 泄漏、无逐句模型 reload；
- 切换中→英→中各 20 次后继续可生成；
- 输出 sample rate / sample count 合法；
- 0.5/1.0/2.0 speed 均可生成；
- 模型缺文件时给出明确缺失文件，不 fallback；
- 记录 1句/100句生成耗时与进程内存变化。

**失败后的安全停止条件**  
如果固定版本出现“同一 engine 第二次/循环生成 crash”，先做最小复现并核对上游已知问题/新版本；在没有验证前不得用“每句销毁重建 engine”作为正式效率方案。需要把最小复现、版本、模型、日志交给 GPT review 后再决定版本 pin。

**推荐模型**  
GPT-5.6 Sol。Native 生命周期、第三方 API、CPU 推理稳定性是关键链路。

---

# P5 — miniaudio 播放状态机与顺序播放

**问题现象**  
`MiniaudioPlayer` 当前是边界占位；旧版 pause/resume/stop 与 TTS 子进程耦合，容易卡死。

**根因 / 待验证原因**  
生成任务、播放 buffer、序列游标、暂停状态需要成为独立状态机，不能用 UI boolean + 线程共享变量堆叠。

**唯一指定的修改方法**  
实现 `PlaybackService`：状态固定为 Idle/Generating/Playing/Paused/Stopping/Error；TTS 合成在 WorkerQueue，PCM 交给 `MiniaudioPlayer`；顺序播放每次只推进一个 row，当前音频结束回调后按 interval 推进下一句。Stop 清空待播放序列并停止 device，不 kill 进程。

**涉及文件 / 模块 / 类 / 函数**  
- `src/adapters/audio/MiniaudioPlayer.*`
- 新增 `src/services/PlaybackService.*`
- `src/core/worker/WorkerQueue.*`
- `src/ui/CorpusRunPanel.*`

**禁止修改范围**  
- 不在 wx GUI thread 调 `Synthesize()`；
- 不 busy-wait；
- 不用 detached thread；
- 不再创建外部 TTS process；
- Stop 后禁止旧任务 callback 恢复成 Playing。

**验收断言**  
- 单句播放、暂停、继续、停止正常；
- 1~1000 行顺序播放能设 start/end/interval/speed；
- 连续快速 Stop→Play 不串状态；
- 当前播放单元格高亮准确；
- 空播放单元格跳过但不导致序列错位；
- GUI 在长句合成时仍可拖动/停止。

**失败后的安全停止条件**  
出现死锁、UI freeze、Stop 后继续出声或旧 callback 越权更新状态，停止 P5，不通过增加 sleep/锁层数掩盖。

**推荐模型**  
GPT-5.6 Sol。状态机+跨线程资源生命周期需要严谨执行。

---

# P6 — 文本自动对齐核心正式化与 UI

**问题现象**  
Seed 已实现 codepoint similarity、anchors、ordered global alignment、MISSING/EXTRA、diff；正式版还需要 utf8proc/RapidFuzz、批量导入 UI 和性能/异常验证。

**根因 / 待验证原因**  
没有 NFKC/casefold 时全角/兼容字符会影响相似度；纯 fallback Levenshtein 在大批量上不是最终性能实现。

**唯一指定的修改方法**  
桌面正式构建强制启用 `utf8proc + rapidfuzz-cpp`。保留现有 `TextNormalizer/TextSimilarity/SequenceAligner/CharacterDiff` 接口，禁止重写边界。Compare UI 提供：正式文本导入、机器文本导入、分隔规则、对齐阈值、OK阈值、开始对齐、结果表。

**涉及文件 / 模块 / 类 / 函数**  
- `src/core/compare/*`
- `src/services/CompareService.*`
- `src/ui/ComparePanel.*`

**禁止修改范围**  
- 不把 80/90 一个阈值同时用于“对齐”和“合格”；
- 不允许一个机器句匹配多个正式句；
- 不低于阈值强配；
- 不修改 raw_text；
- 不用 UTF-8 byte diff。

**验收断言**  
固定测试集覆盖：完全一致、一个字替换、正式句缺失、机器额外句、连续缺3句、重复短句、中文、英文、阿语、全角/半角、大小写、标点忽略开/关。  
1000 vs 1000 真实长度语料在目标公司 CPU 上记录耗时；UI 线程不阻塞。  
每个结果都有唯一 reference index / actual index 或 MISSING/EXTRA。

**失败后的安全停止条件**  
如果真实导出证明“机器文本顺序完全随机而非少量缺插”，当前 ordered aligner 不得偷偷改成任意贪心匹配；先提交失败样本和指标给 GPT review，再决定是否新增独立 unordered one-to-one matcher。

**推荐模型**  
GPT-5.6 Sol 负责算法验收；普通 UI 接线可用 GPT-5.5。

---

# P7 — Excel 富文本报告与旧运行视图导出

**问题现象**  
Seed 的 `LibXlsxWriterExporter` 尚未实现；新需求要求“只把不一致字符标红”，同时旧版运行视图导出不能丢。

**根因 / 待验证原因**  
需要把 `CharacterDiffResult` 转换为 libxlsxwriter rich string fragments，并验证中文/阿语/长文本/空字符串；Windows Unicode 输出路径也要独立验收。

**唯一指定的修改方法**  
只在 `LibXlsxWriterExporter` 实现 XLSX 写入：
- Runtime sheet：按当前运行视图原样写；
- Compare sheet：每语言按“正式/机器/相似度/结果”四列组输出；
- `DiffKind::Changed` fragment 使用红色 font format；
- OK/NG/MISSING/EXTRA 作为独立结果单元格；
- freeze header、autofilter、合理列宽、wrap text。

**涉及文件 / 模块 / 类 / 函数**  
- `src/adapters/excel/LibXlsxWriterExporter.*`
- `src/adapters/excel/IExcelExporter.h`
- `src/core/domain/Types.h`

**禁止修改范围**  
- 不在 Excel exporter 重新算相似度/Diff；
- 不整格染红来替代字符级富文本；
- 不改变 raw 文本；
- 不因为第三方路径问题要求用户改英文文件名。

**验收断言**  
- Excel 打开无修复提示；
- `温度设置为22度` vs `温度设置为23度`：两边差异字符分别标红；
- 删除/插入情况下仅存在的一侧字符标红；
- 中文、英文、阿语显示正确；
- 1000+ 行导出成功；
- 中文目录和中文输出文件名验证；
- 旧运行视图导出仍包含人工 OK/NG 结果。

**失败后的安全停止条件**  
若 libxlsxwriter 对 Windows Unicode output path 在固定版本上有问题，只允许在 adapter 内先写入可控 ASCII staging file，再通过 `std::filesystem`/Win32 W API 原子移动到目标路径；业务层文件名不得降级为 ANSI。

**推荐模型**  
GPT-5.5。格式写入确定性高；Unicode 路径异常再交 GPT-5.6 Sol。

---

# P8 — MOSS-TTS-Nano 100M ONNX CPU adapter

**问题现象**  
用户需要多语言 CPU TTS，MOSS Nano 作为第二 engine 很有价值；seed 只建立 `MossNanoTtsEngine` 边界并故意抛错，避免伪实现。

**根因 / 待验证原因**  
官方 standalone ONNX 路线的推理 orchestration 目前需要按官方参考实现逐步移植到 C++ ONNX Runtime；不能假定“一个 session.run 就等价”。

**唯一指定的修改方法**  
逐函数对照官方 standalone ONNX inference：预处理/tokenizer/生成循环/audio tokenizer 或 decoder/后处理，使用 ONNX Runtime C++ API 重写到 `MossNanoTtsEngine`。建立 Python 官方参考输出作为离线 golden（仅开发验收，Python 不进入发布包），同输入比较 shape、长度、基本声学结果及可播放性。

**涉及文件 / 模块 / 类 / 函数**  
- `src/adapters/tts/MossNanoTtsEngine.*`
- 新增 `src/adapters/tts/moss/*`
- `src/services/ModelRegistry.*`
- `tests/moss_golden/*`（只存小型验收数据，不存 Python runtime）

**禁止修改范围**  
- 禁止运行时启动 Python；
- 禁止把官方 Python 脚本打包进 EXE 并 subprocess；
- 禁止为了“看起来能跑”省略模型步骤；
- MOSS 未验收前不在正式 UI 标记 Ready。

**验收断言**  
- CPU-only 公司电脑可生成；
- 官方支持范围内至少中/英/日/德各固定文本验证；
- 同一输入重复生成行为与官方参考的随机性/seed 语义一致；
- 100句连续生成无资源泄漏；
- 切 sherpa↔MOSS 不需要重启应用；
- 发布目录无 Python/PyTorch。

**失败后的安全停止条件**  
任何 ONNX node/预处理语义无法从官方参考确认时标记 `MOSS_PORT_BLOCKED` 并停在 sherpa 作为正式 engine；不得补猜。

**推荐模型**  
GPT-5.6 Sol。跨 Python 参考实现到 C++ ONNX Runtime 的等价迁移需要较强推理与验收。

---

# P9 — 发布、回归、性能与最终闭环

**问题现象**  
开发机能跑不等于公司无独显电脑可稳定分发；旧工具最大问题之一就是运行依赖和卡顿路径不可控。

**根因 / 待验证原因**  
需要对 release bundle、模型目录、DLL、日志、缓存和异常关闭做最终验证。

**唯一指定的修改方法**  
建立 `package_windows.ps1` 或 CMake install/package（只负责拷贝已固定依赖，不下载安装）。产出独立目录 ZIP；运行时创建 `logs/cache/exports/config`，不写程序目录以外不可控位置。建立最终 smoke/regression checklist。

**涉及文件 / 模块 / 类 / 函数**  
- `CMakeLists.txt`
- `cmake/install*.cmake`
- `scripts/package_windows.ps1`
- `docs/audit/P9_final_acceptance.md`

**禁止修改范围**  
- 不做在线安装器；
- 不自动下载模型；
- 不引入管理员权限；
- 不用缺 DLL 时“调用开发机路径”兜底。

**验收断言**  
在一台没有 VS/Python/Node、没有独显的目标 Windows PC：
1. EXE 启动；
2. 打开中文路径 XLSX；
3. 列分析/映射/运行视图正常；
4. sherpa 中英 voice 各播放；
5. 顺序播放/暂停/继续/停止正常；
6. 人工 OK/NG 正常；
7. 1000句文本对齐；
8. 富文本 Excel 导出；
9. 关闭 EXE 后无残留后台进程；
10. 重新打开配置可恢复。

**失败后的安全停止条件**  
任一验收项依赖开发机隐含环境才能通过，则发布失败，不允许写“已完成”。

**推荐模型**  
GPT-5.5 执行 checklist；最终跨链 review 用 GPT-5.6 Sol。

---

# Codex 每阶段提交格式

每完成一个 P 阶段，回复必须包含：

```text
Stage:
Commit:
Changed files:
Build:
Tests:
Manual acceptance:
Known limitations:
Blocked/Unverified:
Forbidden-scope check:
```

禁止只回复“已完成/测试通过”而不给命令、结果和未验证项。
