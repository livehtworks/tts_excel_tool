# R3 Playback and TTS Acceptance

Date: 2026-08-18

## Scope

- Closed the desktop runtime playback chain from `CorpusRunPanel` to `PlaybackService`, `TtsService`, `SherpaOnnxTtsEngine`, and `MiniaudioPlayer`.
- Fixed playback cancellation and pause semantics across generation, audio playback, and sentence intervals.
- Reused the miniaudio device for consecutive audio buffers with the same sample rate and channel count.
- Re-ran P4 sherpa acceptance with always-on checks, including speed semantics and 500 English plus 500 Chinese generations.

## Verification

- `cmake --build --preset windows-release --config Release`
- `ctest --preset windows-release -R adayo_p5_playback_tests --output-on-failure`
- `ctest --preset windows-release -R adayo_p4_tts_tests --output-on-failure`
  - passed in 385.54 seconds
- `ctest --preset windows-release --output-on-failure`
  - 7/7 passed
  - `adayo_p4_tts_tests` passed in 376.70 seconds
- `cmake --build --preset windows-core --config Debug`
- `ctest --preset windows-core --output-on-failure`
  - 4/4 passed

## Notes

- Playback model loading is strict by `tts_model_id`; there is no hidden language fallback.
- Result-column double-click remains result cycling. Play-column double-click starts single-cell playback through the same production playback path.
