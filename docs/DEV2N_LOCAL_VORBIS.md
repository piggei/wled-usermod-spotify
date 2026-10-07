# dev.2n - local media qualification history

Current build: **dev.2n-r18**. The r17 bounded 64 KiB continuous encrypted AP transport
is hardware-qualified and frozen; r18 changes only SPIRC track-boundary position semantics.
See [DEV2N_R18_SPIRC_POSITION.md](DEV2N_R18_SPIRC_POSITION.md) and
[DEV2N_R17_CONTINUOUS_AP_STREAM.md](DEV2N_R17_CONTINUOUS_AP_STREAM.md).
The qualified AES/Vorbis/audio files and `SpotifyApContinuousRing` remain unchanged. The following sections
preserve local-media qualification history. No live Spotify key reaches the decoder.

# Historical r14 - identity telemetry above the local media baseline

## Purpose

`dev.2n-r14` keeps codec/streaming/crypto engineering separated from the unresolved Spotify AudioKey service response and adds measurement-only AP/ProductInfo identity telemetry. The contiguous local fixture path qualified on hardware in r7, the 4096-byte plaintext chunked path in r8, and the AES-128-CTR -> Vorbis path in r10. No real Spotify AudioKey or captured media content enters the local decoder tests.

r11 factored the chunk producer behind `SpotifyMediaChunkSource`. r12 added `SpotifyApMediaChunkSource`, a bounded transient implementation that retains only the three qualified encrypted AP canary ranges while the live decrypt/decoder consumer remains hard-closed. r13 now exercises that live source through `next()` and `rewindRead()` as a diagnostic-only consumer, comparing its byte/chunk shape and FNV identity with the independent AP source gate. The local plaintext/encrypted fixtures continue to use the same producer contract without changing AES, staging, Vorbis or PCM code.

## Frozen prerequisites

The hardware-qualified r14 baseline remains unchanged: AP/Shannon/Mercury/SPIRC, direct playlist selection, metadata, RequestKey service-block latch/suppression, AP StreamChunk, 50 ms AP receive poll and the Waveshare shared-I2S/ES8311 sink.

## Decoder choice

The qualified decoder target is `esphome/micro-vorbis` v0.1.0. It is a fixed-point Tremor-based streaming Ogg/Vorbis decoder with an arena allocator, PSRAM-aware memory placement and a simple byte-in / interleaved signed-16-bit PCM-out interface. The dependency is declared in `library.json`; its source is not vendored in this repository. Because the usermod is linked into WLED with `symlink://`, r2 required the owning WLED environment to list `esphome/micro-vorbis@^0.1.0` explicitly in `lib_deps`. The real r2 compile still failed at the same include under WLED/Arduino. r3 therefore also sets `lib_compat_mode = off` on **only** `[env:waveshare_spotify]`, disabling PlatformIO's framework compatibility filter for this experimental target while leaving `framework = arduino` unchanged (see `platformio_override.example.ini`).

The WLED/Arduino target compile and final link passed in r6 after the private-include and bundled micro-ogg source bridges were added. r7 then qualified the actual decoder on hardware. Each later revision still requires its own target compile because host contract tests cannot substitute for the target toolchain.

## r1/r2 target-compile results and r3 correction

The first real WLED target compile of r1 failed before any decoder source could compile:

```text
fatal error: micro_vorbis/ogg_vorbis_decoder.h: No such file or directory
```

The upstream include path itself is correct. r1 therefore isolated a dependency-discovery problem. r2 then made micro-vorbis an explicit dependency of `[env:waveshare_spotify]` while preserving `${env:waveshare_esp32s3_32MB_hub75.lib_deps}`, but the target still failed on the same missing header. Since upstream publishes/documents its PlatformIO integration for `framework = espidf`, the repeated r2 failure is consistent with PlatformIO's default framework compatibility filter rejecting the library from the Arduino LDF graph even though it is listed in `lib_deps`.

r3 changes **build compatibility wiring only**: it retains the explicit dependency and adds `lib_compat_mode = off` to the `waveshare_spotify` environment. It does **not** change `framework = arduino`, the board/core, decoder algorithm, fixture, task, PCM contract or any frozen Spotify path.

The next target compile remains a real source-compatibility gate: once the header resolves, any subsequent compile/link error inside micro-vorbis must be captured verbatim and fixed locally rather than hidden by switching the whole WLED environment to ESP-IDF.

## Local fixture

`decoder/SpotifyVorbisFixture.h` contains a generated 10437-byte Ogg/Vorbis file:

- duration: 2.0 seconds;
- sample rate: 44100 Hz;
- channels: 2;
- left: 440 Hz sine;
- right: 660 Hz sine;
- explicit Ogg EOS page.

The fixture is intentionally small and deterministic. It is not Spotify content and contains no credential, token, key or captured media bytes.

## Runtime architecture

```text
embedded Ogg/Vorbis fixture
        |
        +-- contiguous reference (r7)
        |
        +-- r8 transport adapter: 4096 + 4096 + 2245 byte deliveries
                |
                +-- bounded 8192-byte staging / tail preservation
        |
        v
SpotifyVorbisFixturePlayer task (un-pinned, stack 12288)
        |
        +-- micro-vorbis decoder
        |       output: signed 16-bit interleaved stereo / 44.1 kHz
        |
        v
WavesharePcmOutput::enqueuePcm44100()
        |
        v
qualified 44.1 -> 22.05 kHz shared-I2S ingress
        |
        v
ES8311 / speaker output
```

The decode output buffer is 16 KiB and prefers PSRAM. Decoder state placement is controlled by the upstream library configuration/defaults. No decoder work is executed in the WLED loop or in the Spotify AP task.

## Manual test

Start the hardware-qualified contiguous reference fixture:

```text
GET /spotify-test?action=start-vorbis
```

Start the r8 StreamChunk-shaped incremental-input fixture:

```text
GET /spotify-test?action=start-vorbis-chunked
```

Stop any active fixture/test producer:

```text
GET /spotify-test?action=stop
```

The start command stops the existing synthetic PCM/tone producers and flushes the PCM ingress first, preventing two test producers from sharing the sink concurrently.

## PASS criteria

1. Target compile succeeds without changing the frozen WLED framework globally.
2. No reboot, watchdog, stack canary or heap allocation failure.
3. `start-vorbis-chunked` produces the same clean ~2 s audible fixture as the qualified contiguous reference.
4. Completion is `complete-chunked-input-exhausted` or `complete-chunked-eos`, with `complete=1 errors=0`.
5. `vorbis PCM 44100Hz ch=2 input=10437/10437 frames=88200 expected=88200`.
6. `vorbis stream mode=chunked chunk=4096 chunks=3 supplied=10437`.
7. The staging buffer remains bounded (`stageHigh<=8192`) and exactly two refills follow the initial delivery.
8. `feedFail=0` and `feedCalls>0`.
9. `stackMin>0`, output buffer 16384 bytes, memory counters populated.
10. Existing sink telemetry remains free of `err`, `short` and `late`; end-of-one-shot ring underruns are classified separately from decoder/feed failure.
11. After fixture playback, a normal Spotify transfer/direct row selection still passes the r14 control-plane/media-transport gate.

As qualified in r7, explicit decoder EOS is not mandatory after the final successful call. Input exhaustion is accepted only when the complete known fixture has been consumed, the expected PCM format is established and exactly 88,200 frames are produced.

## What this build does not do

- no Spotify AES decrypt;
- no live AP StreamChunk -> decoder coupling; the r8 adapter only reproduces its 4096-byte delivery shape with local fixture bytes;
- no whole-track buffering;
- no CDN/spclient/Dealer/TLS work;
- no change to RequestKey or session identity;
- no music-quality resampler change; the qualified 44.1 -> 22.05 kHz ingress remains intentionally frozen.

## Next gate after PASS

If r8 passes, the decoder-side incremental transport contract is qualified. The next safe engineering step is to factor the staging adapter behind a producer interface that can later accept real AP StreamChunk bytes, while keeping AES integration closed until a valid 16-byte media key is obtained from a legitimate session path. The encrypted Spotify bytes must not be sent to Vorbis before the decrypt stage exists.

## dev.2n-r9 AES-CTR gate

Current librespot's legacy audio decryptor uses AES-128-CTR with a fixed 16-byte IV (`72 e0 67 fb dd cb cf 77 eb e8 bc 64 3f 63 0d 93`) and a 16-byte AudioKey. r9 mirrors that byte-stream contract with `decoder/SpotifyAudioAesCtr.*`, implemented on the ESP32 with the already-linkable mbedTLS AES primitive.

For qualification, `decoder/SpotifyVorbisEncryptedFixture.h` contains the same 10,437-byte synthetic Ogg fixture encrypted offline with a synthetic public key. The source is delivered as 4096 + 4096 + 2245 encrypted bytes. CTR state is continuous across all three deliveries; plaintext is written directly into the existing 8192-byte staging buffer and then decoded by the unchanged micro-vorbis path.

Manual gate:

```text
GET /spotify-test?action=start-vorbis-aes-chunked
```

PASS requires clean audio and:

```text
state=complete-aes-chunked-input-exhausted (or complete-aes-chunked-eos)
complete=1 errors=0
input=10437/10437 frames=88200 expected=88200 feedFail=0
mode=aes-chunked chunk=4096 chunks=3 supplied=10437 refills=2
crypto mode=aes128-ctr keyBytes=16 fixedIv=yes decryptCalls=3 decryptBytes=10437
```

This does **not** solve the server-side `AesKeyError 0:1`; it removes local AES-CTR implementation risk from the future integration. Live encrypted AP StreamChunk bytes remain fenced from the Vorbis decoder until a legitimate 16-byte key is available.

## r4 correction after real r3 target compile

The r3 hardware/toolchain build proved that `lib_compat_mode = off` was sufficient to materialize and compile `micro-vorbis`: PlatformIO reached `src/tremor/*.c` and `src/ogg_vorbis_decoder.cpp`. It then failed because the upstream project relies on ESP-IDF/CMake private include directories that PlatformIO's generic Arduino library builder does not import. The observed missing headers were `ogg/ogg.h` from Tremor sources and `ivorbiscodec.h` from the wrapper.

r4 adds `tools/platformio_micro_vorbis_compat.py`, referenced as a PlatformIO pre-script. It discovers the installed `micro-vorbis` package under `PROJECT_LIBDEPS_DIR`, adds known upstream include roots, scans all packaged headers, and exports header parents (plus their parent namespace directories) through `CPPPATH`. This is deliberately an integration shim: it does not edit `.pio/libdeps`, does not vendor or fork micro-vorbis, does not change WLED away from Arduino, and does not change any runtime decoder or Spotify logic.


## r5 correction after real r4 target compile

The r4 compile did not reach micro-vorbis. The custom `extra_scripts` block in the example override replaced WLED's inherited `${scripts_defaults.extra_scripts}` list. WLED depends on those scripts (including `load_usermods.py`) to prepare usermod/library build context. With them removed, the unrelated built-in `wled_espnow` library failed at `#include "wled.h"`.

r5 fixes only the PlatformIO composition rule: keep `${scripts_defaults.extra_scripts}` and append `pre:../wled-usermod-spotify/tools/platformio_micro_vorbis_compat.py`. The micro-vorbis include bridge itself is retained. This restores normal WLED preprocessing before adding the experimental decoder include paths.


## r6 correction after real r5 final link

The r5 target compile is a major integration milestone: normal WLED preprocessing was restored, the private include bridge worked, Tremor compiled, `ogg_vorbis_decoder.cpp` compiled, and the build reached `firmware.elf`. The remaining failure was a narrow final-link dependency gap: every unresolved symbol belonged to `micro_ogg::OggDemuxer` (`OggDemuxer` constructor/destructor, `reset`, `get_next_data`, and `get_next_packet`).

This matches the upstream architecture: micro-vorbis uses microOggDemuxer as a separate nested CMake subproject. Under WLED/Arduino, PlatformIO recursively builds `micro-vorbis/src` but does not automatically compile `lib/micro-ogg-demuxer/src`, even though its headers are visible. r6 therefore extends the existing pre-script to call `env.BuildSources()` on the **bundled** `lib/micro-ogg-demuxer/src` directory. The bridge fails early with a classified message if that source tree is absent or empty.

The selected micro-vorbis package remains the single source of truth for the demuxer version; r6 does not add a second network dependency and does not copy or patch dependency source. Runtime decoder code and the qualified Spotify r14 baseline remain byte-for-byte unchanged from r5.


## r7 correction after first real decoder playback

The r6 firmware compiled/linked and the Waveshare produced a clean audible local Vorbis fixture. Telemetry proved the codec path itself was complete: 10,437/10,437 input bytes, 44.1 kHz stereo, exactly 88,200 decoded PCM frames, 87 feed calls, zero feed failures, and >10 KiB decoder-task stack high-water reserve. r6 nevertheless reported `missing-end-of-stream` because the adapter required `OGG_VORBIS_DECODER_END_OF_STREAM` even after the final successful decode call had consumed the last caller byte.

The upstream micro-vorbis basic usage loop is bounded by `input_len > 0` and naturally exits when the caller buffer is exhausted; EOS is handled if returned but is not a mandatory extra call after input exhaustion. r7 aligns the fixed-fixture adapter with that contract while remaining strict: input exhaustion is accepted only if all fixture bytes were consumed, the expected 44.1 kHz stereo format was established, and the exact fixture frame count (88,200) was produced. The explicit EOS path is retained and separately telemetered as `eos`; validated input-exhaustion completion is telemetered as `eof`.

The r6 audio snapshot also showed `PCM ingress inFrames=176400` and `flushes=2` versus 88,200 frames in the per-run Vorbis telemetry. Since audio-sink counters are cumulative while fixture telemetry was reset on every start, this is strong evidence that the start endpoint ran twice in that boot. r7 therefore preserves the fixture `starts` counter cumulatively to expose duplicate invocations.


## r8 chunked-input design after r7 hardware qualification

r7 hardware telemetry closed the contiguous decoder gate: `state=complete-input-exhausted`, `starts=1 complete=1 errors=0`, `input=10437/10437`, `frames=88200 expected=88200`, `feedCalls=87 feedFail=0`, and decoder-task `stackMin=10116`. The sink independently reported 88,200 44.1 kHz ingress frames becoming 44,100 22.05 kHz output frames, confirming the frozen 2:1 conversion and exact two-second duration.

r8 deliberately changes only the caller-side input shape. The checked-in fixture is 10,437 bytes and is delivered as 4096, 4096 and 2245 bytes. Its Ogg page layout is 58, 3902, 3249 and 3228 bytes; the third and fourth pages cross 4096-byte source boundaries. The 8192-byte staging buffer therefore retains an unconsumed page tail across a refill and presents the decoder with contiguous bytes only after the next delivery arrives. No whole-track buffer or live Spotify media source is introduced.

## r10 hardware qualification

Hardware on 2026-10-07 closed the local encrypted-media gate. `start-vorbis-aes-chunked` produced clean audible output and reported `complete-aes-chunked-input-exhausted`, `complete=1`, `errors=0`, three 4096-shaped encrypted deliveries totaling 10,437 bytes, `decryptCalls=3`, `decryptBytes=10437`, exactly 88,200 decoded 44.1 kHz stereo frames and zero PCM feed failures. AES cost remained small (`decryptMax=1361us`) relative to Vorbis decode (`decodeMax=21046us`); decoder task stack reserve remained >10 KiB.

This qualifies the local post-key path as:

```text
encrypted chunk source -> AES-128-CTR -> bounded plaintext staging -> Ogg/Vorbis -> PCM -> Waveshare sink
```

It does not alter the server-side `AesKeyError 0:1` result.

## r11 producer abstraction and live pre-decrypt source gate

`decoder/SpotifyMediaChunkSource.h` defines a minimal bounded producer API: reset, next chunk, EOF, total/supplied byte counters and source name. The checked-in fixture uses `SpotifyMemoryChunkSource`; the player owns the staging buffer and optional AES state exactly as before. The qualified r8/r10 chunk semantics therefore no longer depend on direct fixture indexing inside the decoder loop.

The live AP path is intentionally **not** wired into the decoder. While the existing three StreamChunk probes run, r11 updates a rolling FNV-1a hash over the encrypted data bytes, counts bytes, and records whether each completed channel delivered exactly 4096 bytes. No AP media body is copied into persistent storage or exposed through `/json/info`.

Expected source-gate telemetry after a normal Spotify track probe:

```text
AP Stream sourceGate chunks=3/3 mismatch=0 bytes=12288 hash=0x... ready=yes storage=none
```

`ready=yes` qualifies only the producer *shape*. A future live decoder path still requires a legitimate 16-byte AudioKey before any AP media can cross the pre-decrypt fence.


## r12 transient live AP source / hard key gate

Hardware r11 qualification on 2026-10-07 confirmed both sides of the producer boundary independently. The local encrypted fixture remained clean with `source=fixture-encrypted contract=MediaChunkSource`, `decryptCalls=3`, `decryptBytes=10437`, `frames=88200`, and `feedFail=0`. A real Spotify track then produced `AP Stream attempts=3 ok=3`, `sourceGate chunks=3/3 mismatch=0 bytes=12288 ready=yes`, and `macFail=0` while the account again returned `AesKeyError 0:1`.

r12 therefore adds a concrete live implementation without opening DRM consumption. `SpotifyApMediaChunkSource` has a fixed capacity of 12,288 bytes (3 x 4096), accepts packet fragments from the AP task, commits only exact 4096-byte canary channels, and implements the same `reset/next/eof/totalBytes/suppliedBytes/chunksSupplied/name` contract as the local source. Allocation is lazy, PSRAM is preferred, internal heap is the fallback, and the buffer is wiped on reset/invalidation/track-session boundaries.

The old rolling FNV-1a source gate remains independent, so transport-shape qualification does not depend on the new capture buffer. `/json/info` adds only non-sensitive source state:

```text
AP Stream sourceGate chunks=3/3 mismatch=0 bytes=12288 hash=0x... ready=yes telemetryStorage=none
AP Stream liveSource source=ap-encrypted-canary contract=MediaChunkSource ready=yes valid=yes retained=12288/12288 chunks=3 fragments=<n> failures=0 storage=<psram|internal> consumer=closed keyGate=blocked
```

No media bytes are serialized. `keyGate=eligible` is derived only from the existing authenticated RequestKey success state (`audioKeyBytes==16`); r12 still does not pass that key or the live source to the AES/Vorbis player. The invariant for this gate is therefore:

```text
live AP source may be buffered encrypted -> consumer remains closed
```

This is deliberately the final integration step before a future authorized live decrypt coupling.


## r13 diagnostic live-source consumer verification

Hardware r12 on 2026-10-07 qualified transient capture of the real encrypted AP canary: `sourceGate chunks=3/3 mismatch=0 bytes=12288 ready=yes`, `liveSource ready=yes valid=yes retained=12288/12288 chunks=3 fragments=3 failures=0 storage=psram`, while `AudioKey keyBytes=0 err=0:1`, `consumer=closed` and `keyGate=blocked`. Shannon remained `macFail=0`.

r13 does not alter StreamChunk, RequestKey, AES, Vorbis or PCM. After the third 4096-byte range is sealed it reads the live source only through `SpotifyMediaChunkSource::next()` using a 512-byte bounded scratch buffer. Expected shape is 24 reads, 12,288 bytes and 3 logical chunk completions. A rolling FNV-1a digest must equal the independently accumulated AP `sourceGate` digest. The read cursor is then rewound without wiping or changing the sealed encrypted buffer.

Expected telemetry:

```text
AP Stream liveVerify attempts=1 ok=1 fail=0 calls=24 chunks=3 bytes=12288 hash=0x... hashMatch=yes eof=yes rewind=yes decrypt=closed
```

The hard boundary remains unchanged: no live AP bytes are passed to `SpotifyAudioAesCtr` or `SpotifyVorbisFixturePlayer`, no key material is exported to this verifier, and no raw media bytes are exposed in `/json/info`.


## r14 AudioKey identity audit telemetry

Hardware r13 qualified the live source consumer: 24 bounded `next()` reads reproduced exactly 12,288 bytes / 3 chunks and the independent AP FNV digest, reached EOF and rewound while `decrypt=closed`. r14 therefore makes no media-path changes.

r14 exposes the already-sent AP identity classifications and selected non-secret ProductInfo capability tags needed for comparison with current desktop librespot. The ClientHello/auth values and the 42-byte RequestKey layout are unchanged. The new telemetry is diagnostic only and never exports credentials, access tokens, AES key bytes, raw ProductInfo XML or media.

The live AES/Vorbis consumer remains closed regardless of telemetry outcome. Any future identity change requires a separate build and a specific comparative hypothesis.
