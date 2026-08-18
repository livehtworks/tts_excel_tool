# models

模型目录只存模型和 metadata，不在 C++ 中硬编码语言路径。

建议最终约定：

```
models/
  sherpa/
    <voice-id>/
      model.json
      *.onnx
      tokens.txt
      espeak-ng-data/   # 某些 Piper/VITS voice 需要
  moss/
    model.json
    ...
```

`model.json` 的正式 schema 在 P4/P8 固化。UI 通过 ModelRegistry 扫描 metadata 构造语言/Voice 下拉。
