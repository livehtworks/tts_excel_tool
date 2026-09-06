# Persistent Data Facts

The application has no database. Native paths are resolved from the executable directory, not the shell working directory.

| Data | Authority and owner | Producer / consumer | Environment and lifecycle |
| --- | --- | --- | --- |
| Original XLSX | User-owned source; immutable to the application | OpenXLSX reads values, merges, visibility and physical coordinates | Production input; never modified by import, analysis or tests |
| Runtime session | CorpusViewService owns rows, reference owners, edits and result identities | Mapping creates a session; runtime UI/playback/export consume it | In-memory user work; source coordinates distinguish edits from original workbook data |
| config/config.json | ApplicationRuntime is the single in-process authority; JsonConfigStore persists schema 4 | UI requests controlled updates; services consume snapshots | Production settings. Invalid values/future schema refuse overwrite; malformed JSON gets a unique corrupt backup |
| config compare object | Full active CompareOptions snapshot | ComparePanel / CompareService / report metadata | Four versioned profiles plus explicit custom changes. Old root thresholds are retained migration provenance, not a second active option authority |
| cache/tts-v1 | TtsService-owned disposable audio cache | Validated Prepare misses produce float32 WAV + JSON; hits validate identity and checksum | Non-authoritative. 64MiB RAM, default 2048MiB disk, 20000 entries. owner.json identifies ownership; owner.lock excludes other writers; only managed entries may be cleared |
| Cache entry JSON | Cache schema 1, key, fingerprint, audio hash, byte/frame/rate/channel metadata and created/used timestamps | AudioCache validates before decoding PCM and maintains LRU | Recoverable acceleration data. Epochs invalidate pending writes; partial deletion and unknown occupancy are reported honestly |
| model/sherpa/*/model.json and assets | ModelRegistry validates voice identity; TtsService fingerprints actual assets | Registry/engine consume, application does not write | Runtime model dependencies. Canonical resource source remains dist/AdayoCorpusTool/model; isolated review packages copy allowlisted native voices |
| model.json speakers / num_speakers / text_normalization | Voice configuration owns the complete speaker inventory, default ID and explicit input normalization | Resource preparation writes; ModelRegistry expands language/voice choices | Default keeps base ID; other speakers use ::speaker-N. Each choice shares weights but preserves its own speaker/cache identity. NFD character input is distinct from unchanged espeak/pinyin input in cache identity |
| model/piper/voices/*.onnx.sherpa-metadata.json (nested) | Resource preparation provenance and manual recovery information | Preparation records original byte length/hash and prepared hash; downloader checks before reuse | Local authoritative preparation record. ONNX metadata only is appended; native prepared.onnx is a hard link to the same weights. Never overwrite a mismatching journal/file automatically |
| exports / user-selected XLSX | User-requested report snapshots with source/parameter/statistics sheets | LibXlsxWriterExporter produces; users consume | Production artifacts. Preflight limits plus atomic replacement protect existing files on failure |
| logs | FileLogger diagnostic output, never source authority | Runtime/services produce; local diagnostics consume | May contain user paths/errors. Keep local and out of Git |
| Isolated native/review artifacts | Per-run test owners only | Tests and create-only packaging | Disposable test data under unique temporary or external review roots; never substituted for production config/models |

No SQL tables, migrations, dual-write paths or automatic legacy-runtime fallback exist. Playback and compare workers are joined before process-owned services and logs are destroyed.
