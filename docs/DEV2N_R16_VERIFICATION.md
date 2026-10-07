# r16 delivery verification

Baseline: `wled-usermod-spotify-v0.1.0-dev.2n-vorbis-r15.zip`.
Baseline SHA-256:
`04ea40739314bc695fb59e301b8cfde3ef9397f6f3750cf20f6588d5bee481ab`.

The r15 hardware audit, key-target evidence and exercised direct-selection/AP
transport regression are recorded as PASS from the three supplied snapshots.
The r15 local fixture re-test and separate postbuild transcript remain unverified.
Every r16 target/hardware gate remains pending.

## Executed in the preparation environment

| Check | Result |
| --- | --- |
| Complete prebuild manifest | 199 PASS, 0 required failures, 0 optional failures |
| Actual C++11 probe + frozen RequestKey builder, GCC | PASS: 64 named scenarios and 20,000 deterministic event interleavings |
| Same probe suite, ASan + UBSan | PASS, no sanitizer diagnostic |
| Actual AP adapter + HTTP/JSON helpers, GCC, host platform stubs | PASS: activation, generation binding, POST replay, response wiping, cancellation, stale replies, key/counter/latch isolation, preserved reset budget |
| Same adapter suite, ASan + UBSan | PASS, no sanitizer diagnostic |
| Probe and adapter suites, Clang C++11 | PASS; the test-only private-access macro has a local Clang warning exemption, not a production-code exemption |
| Existing real metadata parser, ASan + UBSan | PASS: 48 scenarios, 12,000 mutation/truncation/random inputs |
| r14/r15 frozen files | 24/24 byte hashes identical: all audio/decoder sources, PlatformIO bridge/example, metadata parser and other qualified modules |
| r14/r15 frozen core blocks | 11/11 byte hashes identical: hello/auth, key wire, stream request, old metadata, normal key sender/response/candidate/canary/timeout/stream-receiver paths |
| Firmware marker/source consistency | Current revision and all positive postbuild markers found in runtime source; this is NOT a postbuild run |
| WLED / PlatformIO ESP32 target compile/link | NOT RUN; complete target tree/toolchain absent |
| Firmware ELF/BIN postbuild | NOT RUN; no r16 firmware was generated here |
| Hardware / Spotify key request | NOT RUN; awaiting user |

The native tests execute production C++ for the new logic, not a Python behavioral
rewrite. HTTP/Arduino/FreeRTOS primitives are stubbed; AP network traffic and the
real scheduler are not exercised. Event interleavings and sanitizers do not prove
all possible concurrency schedules or complete metadata/protocol coverage.

The new Probe object measures 416 bytes on the host ABI, with 56-byte summary,
52-byte request and 64-byte per-target result. These are host measurements, not a
claim about the ESP32 linker map. No diagnostic key bytes are stored in the object.

The source archive includes no raw Spotify metadata/audio captures, credentials
or real AES keys. Only the already-qualified local synthetic fixtures remain.
The existing PlatformIO override is unchanged. A successful diagnostic grant would
still be discarded and would not qualify playback; full-track integration remains
separate work.
