# dev.2n-r7 - Local Ogg/Vorbis decoder qualification

## Purpose

`dev.2n-r7` separates codec engineering from the unresolved Spotify AudioKey service response. It decodes a known local, non-encrypted Ogg/Vorbis fixture and feeds the already-qualified Waveshare PCM ingress. No Spotify media bytes or AES material enter this path.

## Frozen prerequisites

The hardware-qualified r14 baseline remains unchanged: AP/Shannon/Mercury/SPIRC, direct playlist selection, metadata, RequestKey service-block latch/suppression, AP StreamChunk, 50 ms AP receive poll and the Waveshare shared-I2S/ES8311 sink.

## Decoder choice

The first target is `esphome/micro-vorbis` v0.1.0. It is a fixed-point Tremor-based streaming Ogg/Vorbis decoder with an arena allocator, PSRAM-aware memory placement and a simple byte-in / interleaved signed-16-bit PCM-out interface. The dependency is declared in `library.json`; its source is not vendored in this repository. Because the usermod is linked into WLED with `symlink://`, r2 required the owning WLED environment to list `esphome/micro-vorbis@^0.1.0` explicitly in `lib_deps`. The real r2 compile still failed at the same include under WLED/Arduino. r3 therefore also sets `lib_compat_mode = off` on **only** `[env:waveshare_spotify]`, disabling PlatformIO's framework compatibility filter for this experimental target while leaving `framework = arduino` unchanged (see `platformio_override.example.ini`).

This is a **candidate until the actual WLED PlatformIO target compile passes**. The upstream PlatformIO example is ESP-IDF-based, while this usermod is built under WLED's Arduino environment on an ESP-IDF 5.x core. Host contract tests cannot substitute for that compile gate.

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

Start the one-shot fixture:

```text
GET /spotify-test?action=start-vorbis
```

Stop any active fixture/test producer:

```text
GET /spotify-test?action=stop
```

The start command stops the existing synthetic PCM/tone producers and flushes the PCM ingress first, preventing two test producers from sharing the sink concurrently.

## PASS criteria

1. Target compile succeeds without changing the frozen WLED framework globally.
2. No reboot, watchdog, stack canary or heap allocation failure.
3. Audible one-shot fixture from the Waveshare speaker path.
4. `vorbisFixture ... state=complete complete=1 errors=0`.
5. `vorbis PCM 44100Hz ch=2 input=10437/10437`.
6. PCM frame count is approximately 88200 frames for the 2 s fixture.
7. `feedFail=0` and `feedCalls>0`.
8. `stackMin>0`, output buffer 16384 bytes, memory counters populated.
9. Existing sink telemetry remains `err=0 short=0 late=0 ringUnderrun=0`.
10. After fixture playback, a normal Spotify transfer and direct row selection still pass the r14 control-plane/media-transport gate.

Input exhaustion without a decoder `END_OF_STREAM` is a failure (`missing-end-of-stream`), even if PCM was produced.

## What this build does not do

- no Spotify AES decrypt;
- no AP StreamChunk -> decoder coupling;
- no whole-track buffering;
- no CDN/spclient/Dealer/TLS work;
- no change to RequestKey or session identity;
- no music-quality resampler change; the qualified 44.1 -> 22.05 kHz ingress remains intentionally frozen.

## Next gate after PASS

Introduce a bounded streaming producer abstraction and replay the same local fixture through chunked input before attaching any Spotify media source. AES integration remains deferred until a valid 16-byte media key is obtained from a legitimate session path.

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
