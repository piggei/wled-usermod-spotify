# WLED Spotify Connect Usermod

Current development build: **v0.1.0-dev.2n-vorbis-r7**

`dev.2n-r7` continues the deliberately independent audio-decoder workstream above the **hardware-qualified dev.2m-r14 baseline**. The Spotify network/control path is frozen: direct playlist selection, AP/Shannon/Mercury/SPIRC, metadata, RequestKey suppression and AP StreamChunk are not changed by this build.

Target baseline: WLED 17.0.0-devV5 / `ESP32-S3_Waveshare_HUB75` on the Waveshare ESP32-S3 RGB Matrix board.

## Current qualified baseline

Real hardware has qualified the receiver through r14:

- direct row selection now resolves `skip_to.track_uid -> URI -> canonical classic TrackRef`, including the observed GID-only queue;
- repeated taps advance TrackRef/GID, metadata and AP Stream exactly once per selection;
- r13 measured the local resolved Load-to-Notify path at about **45-47 ms** after the Load arrives;
- r14 reduced only the idle AP receive poll from 250 ms to 50 ms and retained `macFail=0`, AP Stream success, ~43 WLED FPS and a clean audio sink;
- the affected Premium account still receives `AesKeyError 0:1` for every AudioFile candidate, while encrypted AP StreamChunk transport remains healthy.

The media-key failure is therefore kept separate from decoder engineering. The dev.2i RequestKey wire contract remains frozen until comparative evidence justifies a change.

## Scope of dev.2n-r7

This build adds a **local, non-encrypted Ogg/Vorbis qualification fixture**. It intentionally does not consume Spotify media bytes and cannot bypass the missing Spotify AES key.

- Decoder dependency: `esphome/micro-vorbis ^0.1.0`.
- Fixture: embedded Ogg/Vorbis, 2.0 s, 44.1 kHz, stereo; left channel 440 Hz, right channel 660 Hz.
- Decoder runs in its own unpinned FreeRTOS task (`spotify-vorbis`, 12288-byte stack).
- A 16 KiB decode output buffer prefers PSRAM and falls back to internal 8-bit RAM.
- PCM format is accepted only as 44.1 kHz / 16-bit / 2-channel output.
- Decoded PCM feeds only the already-qualified `WavesharePcmOutput::enqueuePcm44100()` API.
- The fixture must terminate with decoder `END_OF_STREAM`; simple input exhaustion is treated as a failure.
- Telemetry records decode/feed counters, PCM frames, timing, task stack watermark, internal heap and PSRAM deltas.
- No AES, Spotify media body, key, credential or token is added to diagnostics.

Manual control:

```text
/spotify-test?action=start-vorbis
/spotify-test?action=stop
```

Existing controls remain available:

```text
/spotify-test?action=start&tone=1000
/spotify-test?action=start-pcm&tone=1000
/spotify-session?action=stop|reset|probe
```

## Expected hardware gate for dev.2n-r7

After calling `start-vorbis`, the speaker should play the short local fixture once. `/json/info` should then show, at minimum:

```text
vorbisFixture=idle state=complete starts=1 complete=1 ... errors=0
vorbis PCM 44100Hz ch=2 input=10437/10437 frames=<about 88200> feedCalls=>0 feedFail=0
vorbis timing decodeMax=<measured>us feedMax=<measured>us stackMin=>0 outBuf=16384
vorbis memory internal=<before>-><after> min=<min> psram=<before>-><after>
```

The existing audio line must remain clean (`err=0`, `short=0`, `late=0`, `ringUnderrun=0`). A normal Spotify transfer/direct row tap after the fixture must still preserve the r14 network/control behavior.

## Spotify media-key investigation remains open

The second available non-Premium account is **not** a usable AudioKey A/B control: the Spotify app discovers `WLED Matrix` but disables selection with “Passa a Spotify Premium per ascoltare”, so the transfer is blocked before our AP/SPIRC/AudioKey path. Alexa endpoints shown by that account are a different service integration class and are not treated as a protocol model for this usermod.

A separate session-identity audit is documented in `docs/AUDIOKEY_IDENTITY_AUDIT.md`. dev.2n-r7 records the current identity fields but deliberately does not change them; the next AudioKey experiment must be evidence-driven and comparative against a current client implementation.

## Frozen qualified areas

Do not alter without new evidence: shared-I2S/ES8311/DMA, 44.1 kHz PCM ingress, Zeroconf/LoginBlob/persistence, AP DH/Shannon/stored-credential auth, keepalive/Mercury/SPIRC, TrackRef/direct-selection mapping, metadata, dev.2i RequestKey wire/correlation, dev.2l AP StreamChunk transport, and the r14 50 ms AP receive poll.

## Build/test workflow

The source remains an external PlatformIO usermod (`wled-usermod-spotify = symlink://../wled-usermod-spotify`). r1 proved that the dependency declared only inside the symlinked usermod manifest was not visible to the WLED target. r2 added `esphome/micro-vorbis@^0.1.0` explicitly to `[env:waveshare_spotify]`, but the real Arduino target still failed at the same include. That second result is consistent with PlatformIO framework compatibility filtering: upstream micro-vorbis is documented for PlatformIO with `framework = espidf`, while WLED builds this environment with Arduino. r3 therefore keeps the explicit dependency and adds `lib_compat_mode = off` **only to `waveshare_spotify`**, so the package can participate in LDF without changing WLED's framework. See `platformio_override.example.ini`. Decoder/runtime logic is unchanged from r1/r2.

Tests are data-driven through `tests/release_checks.tsv` and `tests/hardware_checks.tsv`; `tools/test_runner.sh` remains generic.

Host/prebuild regression command:

```sh
./tools/test_runner.sh --manifest tests/release_checks.tsv --phase prebuild --repo .
```

The full WLED/PlatformIO target compile is a separate required gate because the current workspace does not contain the complete WLED build tree/toolchain. In particular, `micro-vorbis` must be proven compatible with the target Arduino-on-ESP-IDF environment by the real target compile before dev.2n-r7 can be called hardware-qualified.

## Documentation

- `docs/DEV2N_LOCAL_VORBIS.md` — local Ogg/Vorbis decoder design and hardware gate.
- `docs/AUDIOKEY_IDENTITY_AUDIT.md` — frozen current AP identity and comparative AudioKey investigation plan.
- `docs/DEV2M_MEDIA_KEY_BLOCK.md` — qualified media-key service-block hardening and r12-r14 history.
- `docs/DEV2I_AUDIO_KEY.md` — RequestKey/candidate gate and independent librespot evidence.
- `docs/DEV2L_AP_STREAM.md` — qualified bounded sequential AP StreamChunk transport.
- `docs/NEXT_DEV2_CSPOT.md` — staged continuation after the local decoder gate.
- `THIRD_PARTY_NOTICES.md` — decoder/protocol/cryptographic provenance and licensing notes.

### r8 opaque context-player discriminator

Hardware r7 settled the transport question: tapping another playlist row generated a second classic SPIRC Load (`load=2`) and another field-19 payload, while classic TrackRef index/GID and metadata remained on the current song. Because the resolver could not identify field 19, the Load was then counted as a duplicate and ACKed with current state. r8 therefore does not assume Dealer-only delivery and does not introduce TLS/WSS. It first fingerprints the opaque payload and searches it only for exact URI/GID identities already present in the retained queue. A unique non-current identity is safe to adopt; all other cases remain non-destructive diagnostics.


### r13 timing telemetry / r14 poll optimization

Hardware r12 qualification on 2026-10-07: first track index 3 (`Mama, I'm Coming Home`) established an 84-entry GID-only queue (`1512 / 84 = 18` bytes/ref). Direct taps then resolved uniquely to index 4 (`Passion`) and index 2 (`Close My Eyes Forever`), with `uidResolved=1`, `queueResolved=1`, `ambiguous=0`, metadata attempts 1->2->3, suppressedTracks 0->1->2 and AP Stream attempts 3->6->9, all 3/3 successful. Shannon remained `macFail=0`.

r13 added `SPIRC timing resolveLast/resolveMax/applyLast/applyMax` in microseconds and metadata `rtt/maxRtt` in milliseconds. Hardware measured two direct taps at `resolveLast=38630/36821 us` and `applyLast=47148/45287 us`; metadata RTT was 1503 ms then 255 ms. Since `apply` begins only once the SPIRC Load is received, the local direct-selection path is only about 45-47 ms and does not explain the perceived ~1 s tap-to-visible-change delay.

r14 therefore changes only `SESSION_POLL_MS` from 250 ms to 50 ms, reducing the local idle receive-check ceiling by 200 ms without changing SPIRC parsing, selection, metadata, RequestKey suppression, StreamChunk or audio semantics. The latency itself is not a release blocker; the r14 hardware gate is primarily to prove no stability, reconnect, FPS or audio-sink regression.



### dev.2n-r7 completion semantics after first hardware decode

The r6 target compiled and linked successfully and the real board decoded the complete 10,437-byte fixture to exactly 88,200 PCM frames at 44.1 kHz stereo with `feedFail=0`. The only runtime failure was adapter bookkeeping: the final audio-producing call returned normal success (`0`) while consuming the final input bytes, so r6 labelled the run `missing-end-of-stream`. The upstream micro-vorbis basic loop itself terminates when caller input is exhausted and does not require a separate EOS result on that final call. r7 therefore accepts input exhaustion only when the **entire known fixture** was consumed, the format is valid, and the exact expected frame count was produced. Explicit EOS remains separately counted when reported.

r7 also keeps `starts` cumulative across fixture runs so accidental duplicate GETs are observable. This matters because the r6 snapshot showed `PCM ingress inFrames=176400` and `flushes=2` while the per-run Vorbis telemetry showed 88,200 frames, strongly indicating the one-shot endpoint had been invoked twice in that boot.

### dev.2n-r6 nested micro-ogg link bridge

The real r5 target build reached the final firmware link, proving that the WLED script chain, private include bridge, Tremor sources and `ogg_vorbis_decoder.cpp` all compile under the Arduino target. Link then failed only on `micro_ogg::OggDemuxer` constructor/destructor/reset/data/packet symbols. Upstream micro-vorbis uses `microOggDemuxer` as a separate nested CMake subproject; PlatformIO's generic Arduino library builder compiles `micro-vorbis/src` but does not automatically descend into `micro-vorbis/lib/micro-ogg-demuxer/src`. 

r6 extends the existing compatibility script with `env.BuildSources()` for **the bundled micro-ogg-demuxer source tree shipped inside the selected micro-vorbis package**. It does not fetch a second copy, patch `.pio/libdeps`, change the framework, or alter runtime decoder/Spotify logic. PlatformIO documents `BuildSources()` as the supported pre-script mechanism for adding external source directories to the firmware build.

### dev.2n-r5 WLED script-chain correction

The real r4 target compile exposed an integration error in the example override, not in WLED itself: assigning `extra_scripts` in `[env:waveshare_spotify]` replaced WLED's inherited `${scripts_defaults.extra_scripts}` list. That removed core pre-scripts such as `load_usermods.py`, and the build consequently failed earlier in `wled_espnow.cpp` because `wled.h` was no longer exported to library builders. r5 preserves the complete WLED script chain and appends the micro-vorbis compatibility bridge after it:

```ini
extra_scripts =
  ${scripts_defaults.extra_scripts}
  pre:../wled-usermod-spotify/tools/platformio_micro_vorbis_compat.py
```

No runtime source or qualified Spotify/audio behavior changes in r5.

### dev.2n-r4 target-build bridge

The real r3 compile advanced into `micro-vorbis` and exposed the next packaging mismatch: upstream private CMake include directories were absent under WLED/Arduino (`ogg/ogg.h` and `ivorbiscodec.h` not found). r4 adds only a PlatformIO pre-build include bridge (`tools/platformio_micro_vorbis_compat.py`) that discovers the installed dependency tree and appends its header roots to `CPPPATH`. The downloaded dependency is not patched and the WLED framework remains Arduino.
