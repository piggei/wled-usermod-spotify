# dev.2n-r20 verification

## Host gates

r20 is intentionally a control-plane-only delta over hardware-qualified r19.

Required host checks include:

- actual C++ execution of `SpotifySpircEosPolicy.h` for normal advance, pre-duration, paused, inactive, zero-duration, already-handled generation, repeat and queue-boundary cases;
- static integration checks for idle-socket race ordering, metadata-generation binding, zero-position successor state, one-shot latch and telemetry;
- byte-for-byte freeze of r19 audio, decoder/ring, metadata-audit and AudioKey-probe implementations;
- hash freeze of the two r19 extended-transport implementation blocks inside `SpotifySessionProbe.cpp`.

The normal project prebuild manifest contains 216 prebuild checks. In this preparation environment the checks were executed in four equivalent 54-check manifest chunks (the generic runner is sequential and the full one-shot invocation exceeded the tool execution window): **216/216 PASS, 0 required failures, 0 optional failures**. The r20 native policy test passed with both GCC and Clang under **C++11 and C++17**.

## Hardware status inherited from r19

The 2026-10-07 hardware run qualified r19 on the Waveshare ESP32-S3 matrix: 1 MiB sustained transfer completed 256/256 with matching hashes and 16 write/read wraps; exact final-file tails completed at the AP-reported boundary; deliberate in-flight track change recorded cancellation/wipe and the next track recovered to a complete transfer. r17/r18 semantics and Shannon remained clean.

## Not locally qualified

This preparation environment does not contain the complete parent WLED target tree/toolchain, so `platformio run -e waveshare_spotify`, postbuild firmware-string checks and the r20 hardware auto-next gate remain pending until built/flashed on the target setup.
