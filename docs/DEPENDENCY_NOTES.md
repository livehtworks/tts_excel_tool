# 依赖版本/维护性注意事项

此文件不是在线安装脚本，Codex P1 必须在 Windows 目标环境中形成真正的 dependency lock。

- wxWidgets：使用 3.2 stable 线，不跟随 3.3 development 自动升级。
- OpenXLSX：按 0.5.x API 实现 reader；0.5.1 起项目主仓库迁移到 Codeberg，不能把旧 GitHub master 当未来唯一来源。
- rapidfuzz-cpp：只放在 `TextSimilarity` 后面，不泄漏到业务层。
- utf8proc：正式构建用于 NFKC/casefold/category；seed fallback 只为 Core 无依赖验证。
- libxlsxwriter：只负责写；字符级标红使用 rich string。
- libxlsxwriter：使用仓库 `vcpkg-ports/libxlsxwriter` overlay port，自动补齐安装包缺失的 `include/third_party` headers；不得再要求手工从 vcpkg buildtree 复制。
- sherpa-onnx：固定验证版本，C API 全部隔离在 adapter；必须做重复推理稳定性压测。
- miniaudio：只负责音频设备/PCM 播放，不承载业务播放队列。
