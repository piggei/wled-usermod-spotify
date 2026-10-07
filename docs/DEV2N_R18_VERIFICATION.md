# dev.2n-r18 verification

## Scope

r18 is intentionally control-plane only. It fixes stale SPIRC position inheritance when a
new track identity is selected and adds reason/counter telemetry. The qualified r17 media
pipeline remains frozen.

## Host checks

The prebuild manifest includes a dedicated r18 regression check that verifies:

- context-player direct selection builds the changed track at position zero;
- direct full LOAD after local activation resets a changed GID while initial transfer can
  preserve its remote position;
- REPLACE, PLAY-by-index and NEXT/PREV changed identities use the same reset reason;
- NEXT/PREV with no actual index change preserves the current position;
- Pause/Resume/PlayPause and Seek remain current-track operations;
- `/json/info` exposes `source=` and `trackResets=`;
- every file under `audio/` and `decoder/`, plus the r15/r16 metadata/key diagnostic
  implementation files, is byte-identical to r17.

The retained r17 freeze contract continues to verify the r16 AudioKey/metadata baseline and
the encrypted continuous-transport fence.

## Target-build limit

The complete WLED/ESP32-S3 target tree/toolchain is not present in the preparation workspace,
so a real `waveshare_spotify` compile/link and the hardware position test remain required.

## Preparation result

The complete prebuild manifest passed with **208 PASS, 0 required failures and 0 optional
failures**. The retained r17 continuous-ring native test passed with both GCC and Clang;
GCC ASan/UBSan also passed. The frozen r16 key-probe and adapter tests and the r15 metadata
audit passed again under GCC ASan/UBSan.

No complete WLED/ESP32 target build was run in the preparation environment; that remains
the first hardware gate for r18.
