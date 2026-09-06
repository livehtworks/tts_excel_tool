# AdayoCorpusTool 当前问题与 GPT 调研交接

日期：2026-09-06。面向下一轮技术调研，不是已批准的架构迁移方案，也不代表以下问题已修复。
交接前已提交基线为 `9e204ac`；本次提交包含后续声音配置源码、资源清单和本文件。

## 1. 调研目标与约束

请针对下述已确认缺口提供可实施、可验收的完整解决方案，分别说明确定事实、推断和待验证项。
重点是 Piper 全量已下载声音的原生支持，以及 MOSS-TTS-Nano 的真实原生推理接入；不要将下载完成、配置存在、能出声、发音正确混为一谈。

- Windows C++20 + wxWidgets + CMake/Ninja/MSVC，不改成 Python 应用，不附带 Python 生产运行环境。
- Python 仅用于开发期资源准备、测试和参考实现对照。现有 ONNX Python 包不是程序运行依赖。
- 保持 ApplicationRuntime、TtsService、PlaybackService 各自的配置/生命周期、缓存、播放所有权；不得旁挂第二套服务或隐藏回退。
- 当前唯一声音资源根为 `dist/AdayoCorpusTool/model`。模型、词典及辅助前端资源分类存放于此，避免第二套物理权重副本。
- 大模型/声音资源优先国内源、下载不走代理；普通开发工具依赖允许代理。镜像必须核对官方来源、版本和校验值。
- 不得凭修复需要擅自删除声音、替换音色、重写用户数据、发布覆盖或打 ZIP。架构变更和声音替换方案需要单独确认。
- 本轮授权仅汇总、commit/push 源码；没有执行以下新方案，也没有覆盖已有 EXE。

## 2. 当前工程和真实调用链

程序主要用于读取 Excel 语料、映射多语言列、生成播放任务并记录比对结果，以及文本比较和 XLSX 导出。

当前语音链：

```text
ApplicationRuntime
  -> ModelRegistry(exe_dir/model/sherpa): 读取各声音 model.json，展开 speaker 条目
  -> TtsService: 模型身份校验、加载、合成、内存/磁盘音频缓存
  -> SherpaOnnxTtsEngine: Sherpa-ONNX C API，VITS/Piper，CPU
  -> Sherpa 内部文字前端 -> 音素编号 -> ONNX 推理 -> PCM
  -> PlaybackService -> MiniaudioPlayer
```

原生依赖当前采用 Sherpa-ONNX 1.13.6。现有适配器调用 `SherpaOnnxOfflineTtsGenerateWithConfig` 输入文本，并未接入完整的最新版 Piper 参考推理程序。
`MossNanoTtsEngine` 的 Load/Synthesize 当前显式报未实现；ApplicationRuntime 没有将其作为可用引擎接入。

关键入口：

| 文件 | 调研关注点 |
| --- | --- |
| `src/app/ApplicationRuntime.cpp` | 服务所有权、模型目录、唯一引擎注册 |
| `src/services/ModelRegistry.cpp` | 配置校验、语言及多说话人条目 |
| `src/services/TtsService.cpp` | 资源身份、引擎装载、合成和缓存一致性 |
| `src/adapters/tts/SherpaOnnxTtsEngine.cpp` | 当前 C API、NFD 输入处理、音频返回 |
| `src/adapters/tts/MossNanoTtsEngine.cpp` | 当前显式阻塞，并非可用 MOSS 实现 |
| `scripts/prepare_downloaded_voices.py` | 元数据、tokens、依赖硬链接、真实探针后注册 |
| `scripts/download_piper_voices_modelscope.py` | 国内源下载、已准备权重的保护 |
| `tests/p4_tts_tests.cpp` | 真实原生合成、注册清单检查、独立进程路径门禁 |
| `tests/fixtures/voice_smoke_samples.json` | 当前各 locale 的出声检查文本，不是发音金标准 |

## 3. 下载与可用性快照

| 指标 | 当前结果 |
| --- | ---: |
| 已下载 Piper 模型/质量规格 | 174 |
| 按 locale/voice 区分、未重复计质量规格的声音数据集 | 152 |
| 基础语言 / locale | 50 / 55 |
| 所有规格的 speaker 槽位 | 2707 |
| 已配置且通过原生出声检查的下载规格 | 171 |
| 对应逐 speaker 合成检查 | 2703 |
| 尚不能启用的下载规格 | 3，共 4 个 speaker |
| 171 个中出现未知音素告警的规格 | 6 |
| 额外既有 Xiao Ya INT8 声音 | 1 |
| 加上 Xiao Ya 后的原生配置 / speaker 条目 | 172 / 2704 |
| MOSS 组件 / 文件 / ONNX 图 | 2 / 22 / 8 |

这里的 speaker 槽位不是去重后的人数。171 个中的 165 个在当前样本上未观察到上述告警，也不代表任意输入或发音质量已经通过验收。
MOSS 两个组件是 TTS 与音频 tokenizer，不是两个声音包；资源结构检查通过不等于原生推理可用。

阿语两个 Kareem 规格（ar-JO）和西语九个规格（es-AR/es-ES/es-MX）已经配置并合成成功，不再属于“声音没下载/缺原生配置”的阻塞。它们的听审、业务 ASR 准确率仍未证明。

详细机器清单见 [VOICE_RESOURCE_INVENTORY.json](VOICE_RESOURCE_INVENTORY.json)。其中 PASS 的精确定义是加载成功并生成有限、非静音 PCM，不是发音等价或人工验收通过。

## 4. 三个尚未启用的声音

### TTS-01：英语 Mike 的组合音素

- 资源：`piper/voices/en/en_US/mike/medium/en_US-mike-medium.onnx.json`，1 speaker。
- 原始 `phoneme_type=espeak`，明确配置 `vowel_clusters`：`aɪ`、`aʊ`、`eɪ`、`oʊ`、`ɔɪ`。
- 当前 Sherpa 的 Piper token 读取按单 Unicode 码点存储。原生试验在 `aɪ 161` 上因长度为 2 拒绝加载；配置准备脚本现显式阻止此模型注册。
- 官方流程在 NFD 拆分后，按显式组合配置进行最长匹配合并，再使用原模型编号。仅删除组合 token 或仅放宽读取检查都不等价。
- 候选方向：在现有原生 Piper 链完整支持组合 token/合并/编号，或采用经验证确实具备该能力的上游版本。不是直接认定“升级即可”。
- 请调研：固定版本的具体改动点、C API/内部接口边界、现有单码点声音回归、BOS/PAD/EOS 保持、配置和缓存身份如何传递。必须给出参考音素及 ID 序列对照。

### TTS-02：日语 hi_fi_captain 的前端

- 资源：`piper/voices/ja/ja_JA/hi_fi_captain/medium/ja_JA-hi_fi_captain-medium.onnx.json`，2 speakers。
- 原始 locale 是 `ja_JA`，`phoneme_type=japanese`，不能静默重命名，也不能冒充通用 espeak 模型。
- 官方参考使用 OpenJTalk 全上下文标签，解析读音、音高升降、重音短语边界，再转换到训练所用的 IPA/音素编号。
- 候选方向：接入 OpenJTalk 原生库与对应词典，在现有 TTS 所有权下实现相同的前端流程。
- 请调研：Windows/MSVC 可重复构建、词典版本/来源/许可/校验值/大小、准确的标签处理及 IPA 映射、所需 Sherpa 接口改造。不能停留在“调用 OpenJTalk 得到读音”。
- 验收文本必须覆盖汉字、假名、助词、长音、促音、数字、问句、标点和短语重音；逐步对照标签、音素和 ID，最后听审两个 speaker。

### TTS-03：希伯来语 saspeech 的前端

- 资源：`piper/voices/he/he_IL/saspeech/medium/he_IL-saspeech-medium.onnx.json`，1 speaker。
- `phoneme_type=hebrew`。需要 Nakdimon 补元音标记（niqqud），再按规则转换为 IPA。
- 官方当前参考代码使用 `nakdimon.onnx` 和 ONNX Runtime，有明确的字符编码、三个输出头及标记合并流程；可以作为原生实现依据，不意味着本地已具备该辅助模型。
- 候选方向：C++/ONNX Runtime 执行元音标注，原生实现匹配的预处理、结果还原及 IPA 转换。
- 请调研：辅助模型来源/许可/版本/大小/国内镜像、完整字符表和张量契约、已有标注文本的行为、标记顺序、句子边界，以及如何进入同一原生合成链。
- 验收同时覆盖带/不带元音标记输入，比较标注结果、IPA 和 ID；不得用泛化 espeak 希伯来语替代模型实际训练前端。

**上述三个的声音权重已经存在，但特殊前端的辅助模型/词典尚未完成单独的依赖闭合审查。** “声音已下载”不能用作“所有依赖均齐全”的结论。

## 5. 六个可出声但有音素告警的规格

### TTS-04：原始编号表与当前音素输出不一致

| 规格 | 日志中的未知码点 | 可能受影响的信息 |
| --- | --- | --- |
| `fr_FR-gilles-low` | U+0303 | 鼻化 |
| `fr_FR-mls_1840-low` | U+0303 | 鼻化 |
| `fr_FR-siwis-low` | U+0303 | 鼻化 |
| `vi_VN-25hours_single-low` | U+0032 (`2`) | 声调标记 |
| `vi_VN-vivos-x_low` | U+0032 (`2`) | 声调标记 |
| `zh_CN-huayan-x_low` | U+0032 / U+0035 (`2` / `5`) | 声调标记 |

本地原始六份 JSON 都记录 `piper_version=0.2.0`，`phoneme_id_map` 确实缺少表中符号。真实日志记录 `Skip unknown phonemes`，之后仍返回非静音 PCM。
“旧模型”只是此前的简写，不表示过期应删、文件损坏或所有 low/x_low 规格有问题。

已证明：当前流程丢弃了这些符号。尚未证明：具体可听误读程度、是否由历史前端版本不匹配造成、官方原始推理是否同样丢弃、模型权重是否需要修订。

请按以下分支调研，不要预设根因：

1. 确定这些具体资源的官方来源、配套 Piper/eSpeak 代码与数据版本，比较官方和现有前端的音素及 ID 输出。
2. 若只是前端版本/表示/归一化差异，给出依据模型训练契约的修复，证明不丢失鼻化与声调信息。
3. 若官方原流程也丢弃，明确区分“复现官方行为”和“达到发音质量要求”；查官方修正版。确需更换声音、模型修订或重训时，单列成本、证据和需用户决定的范围，不静默替代。
4. 方案不得随意新增 token ID、改成无关 ID、吞日志或把人工听审替代可自动证明的序列一致性。

## 6. MOSS 原生推理缺口

### TTS-05：资源完整不等于 MOSS 已接入

- 本地有 `MOSS-TTS-Nano-100M-ONNX` 与 `MOSS-Audio-Tokenizer-Nano-ONNX`，22 文件、8 ONNX 图；结构及外部权重范围检查通过。
- 当前 `MossNanoTtsEngine` 仍显式拒绝 Load/Synthesize；其阻塞测试通过只证明不会假装启用，绝不证明 MOSS 合成通过。
- 已知缺口包括官方多图推理顺序和状态管理、SentencePiece 等价、音频 tokenizer 协作，以及生产入口的引擎选择/注册/配置贯通。
- 请以这些已下载资源对应的官方 manifest 和参考推理代码为准，列出所有输入输出 dtype/shape、状态与 KV cache（若该图使用）、采样/EOS、音频解码、参考音频/提示契约（以官方实际支持为准）。不要从其他 MOSS 型号推断当前模型行为。
- 原生图级/序列级等价完成后，仍需接入 TtsService 的资源身份、缓存、错误、取消和模型切换；不得另建绕开现有生命周期的后台推理服务。
- 先输出可验证的完整技术方案。不得将组件数说成 voice 数，也不得把本任务扩展成任意模型训练或新增云服务。

## 7. 跨项工程与验收边界

### TTS-06：缓存、资源安全和交付完整性

- 新前端的代码版本、规则、词典、辅助模型及推理参数都要进入有效资源/缓存身份，防止修复后命中旧音频。
- 当前 170 个准备过的 ONNX 权重与运行时路径使用 NTFS 硬链接，共享物理文件；曾核对原始字节前缀和准备后 SHA256。不能把两个路径当两份独立备份，也不能直接编辑一条硬链接而忽略其他引用。
- 元数据准备保持推理图不变，有 `.sherpa-metadata.json` 来源记录；下载器识别此记录，拒绝自动覆盖不匹配资源。后续重做资源准备前，应审查重跑、失败恢复、已有配置/文件备份与完整发布边界，不对权威 model 目录做试验性写入。
- 日语/希伯来语等新增依赖需同时审查源码、二进制、词典、权重的许可证和再分发条件；当前源代码发布不代表已获全量 voice 再分发许可。
- 现有打包脚本仍是两模型 allowlist，不限制运行时扫描，但也不是全量声音的便携发布方案。用户要求未来统一发布位置为 `dist/AdayoCorpusTool`、自行压 ZIP；脚本目前 create-only，后续受控更新方案须明确保留 model/config/cache 等用户现场，不能直接强制覆盖。
- 历史 `_download_test` 只有 tokens.txt，无 model.json，注册扫描会报一个诊断。它不计入 174 个声音，也没有擅自删除；不是缺下载的一种声音。

### QA-01：已有业务验收仍有未完成项

此前工作包 36 项矩阵为 29 PASS / 7 NOT_RUN，不是整体 PASS。当前仍需完成：

| 项目 | 未完成的验证范围 |
| --- | --- |
| PLAY-04 | 冷/热生成、播放、间隔、交接时的暂停/继续/重复停止/新播放/关闭完整组合，关联实际音频输出；人工听审单列 |
| CACHE-07 | 实际音频排队、生成、播放期间清缓存的 GUI 全链验证 |
| XLSX-04 / VIEW-02 | 快速切文件/表/表头及过期回调，多语言编辑、范围、源列、游标组合 |
| CMP-04 / LIFE-01 | 长比较及各后台任务期间取消、关闭、重开完整矩阵 |
| ENV-02 | 未保存所有既有生产/backup 文件的完整操作前哈希，不能事后补造证据 |

阿语/西语的旧“资源阻塞”已解除，但全量新增语言的听审、业务 ASR 输出和准确率仍未验证。这些不是已确认 UI 缺陷，而是尚缺的验收证据，不能写成已修复或测试通过。

## 8. 已完成工作与证据口径

- 已实现多 speaker 注册、配置/路径校验、乌克兰语字符前端 NFD、归一化参与缓存身份、中文/非 ASCII 模型路径的 UTF-8 进程声明与 fresh-process 门禁。
- 上一轮真实全量声音探针覆盖 171 规格/2703 speaker；完整 Release 7/7（包括 500 EN + 500 ZH 合成）、Core 4/4、准备脚本 8/8。保留六个音素告警，不将上述数字视作发音验收。
- 本次提交前重新执行 Release 针对性 6/6（不含耗时 P4 合成）、Core 4/4、准备脚本 8/8；没有重新执行 2703 speaker 或全套 1000 次合成。
- 本次另用生产 ModelRegistry 只读复核：171 个下载模型、2703 speaker，加上既有 Xiao Ya 共 2704 条目；唯一诊断仍为历史 `_download_test` 缺配置。
- 本次没有新实现 TTS-01 至 TTS-05，也没有宣称 QA-01 闭环。

可随源码审阅的证据索引：

- [VOICE_RESOURCES.md](VOICE_RESOURCES.md)：资源状态与准备方式。
- [VOICE_RESOURCE_INVENTORY.json](VOICE_RESOURCE_INVENTORY.json)：174 个资源条目、阻塞、告警、摘要和 MOSS 结构清单。
- [PROJECT_STATUS.md](PROJECT_STATUS.md)、[EXECUTION_NOTES.md](EXECUTION_NOTES.md)、[DATA_FACTS.md](DATA_FACTS.md)：当前事实和环境边界。
- [REVIEW_EXECUTION_20260906.md](REVIEW_EXECUTION_20260906.md)：此前验收历史；其中阿语/西语阻塞以本次当前快照为准。

仅存在本机、不随源码 push 的证据：

- `logs/voices-all-20260906`、`logs/voices-repaired-20260906`、`logs/voices-pt-utf8-20260906`：逐声音合成日志。
- `logs/voice-registry-inventory.log`、`logs/voice-resource-integrity.json`：注册与权重/硬链接检查。
- `logs/research-handoff-registry.log`：本次提交前的只读注册复核。
- `logs/voice-release-utf8-tests.log`：上一轮完整 Release；`logs/research-handoff-{release,core,preparation}-tests.log`：本次提交前检查。
- 私有工作簿、GUI 截图、录音和导出物不进入 Git。远程审阅者不能据文档索引声称已亲自检查本机原始证据。

## 9. 请 GPT 返回的调研成果

1. 对 TTS-01 至 TTS-05 逐项给出根因、参考实现、不可省略的处理步骤和待实测问题；区分已证实方案与候选方案。
2. 在现有技术底座下选择明确的原生实现路径；如建议升级/修改 Sherpa 或更换接入方式，给出版本证据、接口差异、所有受影响功能的完整承接与人工回滚方案，而非只改模型加载。
3. 给出依赖清单：精确版本/提交、官方与可核验国内下载地址、校验值、体积、许可证、C++/MSVC 可用性。未核实信息不得编造。
4. 给出各阶段文件级改动范围、真实入口接入条件、缓存/生命周期/错误策略、资源更新与恢复步骤。
5. 给出音素/ID/张量金标准、真实合成与听审样本、已可用 171 个声音的回归策略；声明哪些无法自动验收。
6. 对六个音素告警明确回答：前端不匹配还是官方模型也有相同限制；若只能换音色/修订模型/重训，单独列为用户决策，不能伪装成配置修复。
7. 将已知缺陷、能力缺口、待验证风险、历史证据不足、需用户验收分别列出，最后给出阶段顺序及有依据的工作量估计。

## 10. 官方调研起点

以下是本轮已查阅的官方来源。`main` 是可变分支，后续实现必须固定匹配资源的提交和依赖版本，不能把当前 main 自动视为所有旧声音的训练契约。

- [Piper 配置及 phoneme_type / vowel_clusters](https://github.com/OHF-Voice/piper1-gpl/blob/main/src/piper/config.py)
- [Piper eSpeak/NFD/组合音素处理](https://github.com/OHF-Voice/piper1-gpl/blob/main/src/piper/phonemize_espeak.py)
- [Piper 日语 OpenJTalk/IPA/重音流程](https://github.com/OHF-Voice/piper1-gpl/blob/main/src/piper/phonemize_japanese.py)
- [Piper 希伯来语分流](https://github.com/OHF-Voice/piper1-gpl/blob/main/src/piper/phonemize_hebrew.py)
- [Piper Nakdimon ONNX 预处理与输出还原](https://github.com/OHF-Voice/piper1-gpl/blob/main/src/piper/hebrew/__init__.py)
- [Sherpa 1.13.6 Piper token 读取及未知音素处理](https://github.com/k2-fsa/sherpa-onnx/blob/v1.13.6/sherpa-onnx/csrc/piper-phonemize-lexicon.cc)
- [Sherpa 1.13.6 Piper 元数据转换](https://github.com/k2-fsa/sherpa-onnx/blob/v1.13.6/scripts/piper/add_meta_data.py)

MOSS 的调研必须从本地 manifest 指向的对应官方模型版本核对开始，本文件不以其他型号的论文或演示代替当前八张 ONNX 图的接口契约。
