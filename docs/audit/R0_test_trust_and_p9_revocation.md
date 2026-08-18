# R0 Test Trust And P9 Revocation

Status: completed with downstream R1 failure exposed.

## Changes

- Replaced standard `assert()`-based checks in test executables with always-on `REQUIRE` macros from `tests/TestCheck.h`.
- Wrapped test `main()` functions with common exception handling so failures return non-zero in Debug and Release.
- Split the P6 NFKC assertion by capability:
  - with `ADAYO_HAS_UTF8PROC`, full-width/half-width NFKC must pass;
  - without `ADAYO_HAS_UTF8PROC`, the NFKC-specific check is reported as capability not enabled.
- Revoked current P9 acceptance status until R0-R6 are complete.

## Evidence

- `rg -n "assert\\(|#include <cassert>" tests` returns no matches.
- A temporary `REQUIRE(false)` probe returned exit code `1` both with and without `/DNDEBUG`.
- `windows-core` Debug tests passed: `4/4`.
- `windows-release` now executes real checks and exposes the known R1-1 failure:
  - `adayo_p2_tests` fails because `ENG结果` is classified as `Utterance` instead of `Result`.

## Next Required Stage

Proceed to R1, starting with R1-1 result-column recognition.
