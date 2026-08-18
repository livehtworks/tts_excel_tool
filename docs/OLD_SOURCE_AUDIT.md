# 旧源码审计摘要

审计对象：用户上传 `tts_excel_tool_code_20260818_141950.zip`，以最新 `app.py` 为主。

## 已确认事实

- 最新 `app.py` 约 2500 行，单文件同时承担：FastAPI 路由、内嵌 HTML/JS、Excel 读取、全局运行状态、配置、TTS backend、播放控制、导出。
- 旧运行状态使用一个全局 `runtime_state` + lock。
- Excel 分析使用 `openpyxl.load_workbook` 后 `list(ws.iter_rows(...))`。
- TTS 曾经历 pyttsx3 → PowerShell/System.Speech → MMS/XTTS/Kokoro 路由 → Piper subprocess；多个 backend 失败后会自动回退占位 TTS。
- Piper 路径通过 `piper.exe` + 临时 wav + Windows 播放；这不是新架构要保留的业务语义。
- 最新业务包含 Sheet、header row、列分析、映射持久化、多行展开、运行视图、单句/顺序播放、暂停继续停止、语速、结果标记、运行视图编辑、XLSX 导出。

## 直接性能风险

1. **逐句外部进程启动**：Piper/PowerShell 路径产生进程创建成本。
2. **临时 WAV 链**：生成→磁盘→读取/播放，增加 IO 与资源管理复杂度。
3. **浏览器/HTTP 层**：对于单机原生工具只是额外状态同步与部署层。
4. **大单文件状态耦合**：任何 TTS/Excel/UI 修改都触碰同一全局状态链，维护和并发故障定位成本高。
5. **大运行时依赖**：PyTorch/Transformers/TTS 对“无独显的本地语料播放工具”不是合理默认依赖。

## 重写策略

不复用上述实现，只复用 `BUSINESS_BASELINE.md` 的业务行为。新代码严格按 Core / Service / Adapter / wxWidgets UI 分离。
