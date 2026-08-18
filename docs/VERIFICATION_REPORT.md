# Seed 包验证报告

生成环境：Linux container，仅验证纯 C++ Core；没有伪造 Windows/wxWidgets/sherpa/Excel adapter 通过状态。

## 已执行

```bash
cmake -S . -B build -DADAYO_BUILD_DESKTOP=OFF -DADAYO_BUILD_TESTS=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

结果：`1/1 adayo_core_tests PASS`。

## 当前自动测试覆盖

- UTF-8 中文/阿语/英文 roundtrip；
- code point 相似度；
- 字符替换双侧 Diff；
- 中间缺句后的序列恢复；
- 旧版多行播放 cell 展开；
- 参考列重复；
- 列语言/类型猜测基本样本。

## 明确未验证

- Windows wxWidgets GUI 编译；
- OpenXLSX 0.5.x adapter；
- libxlsxwriter rich string adapter；
- sherpa-onnx v1.13.x Windows native link / 连续推理；
- miniaudio device lifecycle；
- MOSS-TTS-Nano ONNX C++ port；
- Windows 中文路径完整端到端；
- 目标公司 CPU 上性能。

这些项目已分别进入 P1/P2/P4/P5/P7/P8/P9，不应在当前 seed 被标记为完成。
