# P5 Playback Service Audit

Date: 2026-08-18

## Scope

- Implemented `MiniaudioPlayer` for float PCM playback through miniaudio.
- Implemented `PlaybackService` state machine.
- Added focused playback service tests with fake TTS/audio dependencies.

## State Machine

States:

- `Idle`
- `Generating`
- `Playing`
- `Paused`
- `Stopping`
- `Error`

Generation and playback are submitted to `WorkerQueue`; the wx GUI thread must not call `TtsService::Synthesize()` directly.

## Acceptance

- Sequence playback skips empty text items without cursor drift.
- Current row/column are updated from playback items.
- Pause and resume call the player and update state.
- Stop increments a generation token so stale worker callbacks cannot restore `Playing`.
- Playback errors are stored as explicit `Error` state text.
- `MiniaudioPlayer` consumes `AudioBuffer` float PCM directly and does not create WAV temp files.

## Commands

```cmd
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
cmake --build --preset windows-release
ctest --preset windows-release --output-on-failure
```

Result:

```text
1/4 adayo_core_tests PASS
2/4 adayo_p5_playback_tests PASS
3/4 adayo_p2_tests PASS
4/4 adayo_p4_tts_tests PASS
100% tests passed
```

## Manual Notes

- Real device playback depends on the target Windows audio device. The deterministic automated test uses a fake player so it does not depend on speakers being present.
