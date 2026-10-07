# WLED Spotify Connect Usermod

Current development build: **v0.1.0-dev.2n-vorbis-r17**

`dev.2n-r17` freezes the AudioKey investigation after an independent current
librespot build reproduced the same `AesKeyError 0:1` for the same primary
GID/file pairs on Windows. The current go-librespot master was also audited:
its public PlayPlay plugin remains a stub (`IsSupported() == false`) and falls
back to the legacy AP AudioKey provider. r17 therefore does **not** mutate the
RequestKey wire, AP identity, country, candidate selection or the r16 manual
probe. Instead it advances the independent media-transport workstream with a
bounded **64 KiB continuous encrypted AP diagnostic**.

Target baseline: WLED 17.0.0-devV5 / `ESP32-S3_Waveshare_HUB75` on the Waveshare ESP32-S3 RGB Matrix board.

## Current qualified baseline

Hardware has qualified the following components independently. Full Spotify-track playback and the live integration are still separate, unqualified gates:

- direct row selection resolves `skip_to.track_uid -> URI -> canonical classic TrackRef`, including the observed GID-only queue;
- r14 retains `macFail=0`, successful AP StreamChunk transport, ~43 WLED FPS and a clean shared-I2S sink with a 50 ms AP receive poll;
- r7 decodes the 10,437-byte local Ogg/Vorbis fixture to exactly **88,200 PCM frames at 44.1 kHz stereo**;
- r8 replays the same fixture as **4096 + 4096 + 2245 byte** deliveries with Ogg pages crossing transport boundaries;
- r10 decrypts a synthetic encrypted fixture with **AES-128-CTR**, then decodes it through the same chunked Vorbis path to exactly 88,200 frames with `errors=0`, `feedFail=0` and clean audible output;
- r16 proved six bounded manual primary/alternative key requests were actually sent and all were rejected `0:1`; a freshly built upstream librespot `dev` checkout then reproduced `0:1` for the same primary GID/file pairs and additional tracks on the same Premium account.

The legacy media-key rejection is now treated as an external/open interoperability
dependency rather than a reason to keep perturbing the qualified WLED RequestKey.

## Scope of dev.2n-r17

The existing three-range canary remains frozen and runs first. Only after its
12,288-byte source gate and `MediaChunkSource` hash/read verification pass does
r17 start a second diagnostic phase for the same preferred audio file:

```text
16 sequential AP StreamChunk ranges x 4096 bytes = 65536 encrypted bytes
        -> SpotifyApContinuousRing (64 KiB cap, PSRAM preferred)
        -> diagnostic consumer only
        -> producer/consumer FNV identity check
        -> no AES key, no decrypt, no Vorbis, no PCM
```

The ring enforces exact monotonic absolute offsets, classifies gaps and duplicates,
never overwrites unread bytes under capacity pressure, wipes bytes as the diagnostic
consumer drains them and is reset on track/session boundaries. Each received packet
is drained immediately, so the test exercises a producer/consumer path rather than
merely retaining a 64 KiB blob.

Expected telemetry includes:

```text
AP Continuous state=... target=65536 received=... consumed=... ranges=.../16 pending=...
AP Continuous transport attempts=... ok=... failures=... timeouts=... protoErr=...
AP Continuous ring storage=psram cap=65536 highWater=... buffered=... produced=... consumed=... backpressure=... gap=... duplicate=...
AP Continuous integrity producerHash=0x... consumerHash=0x... match=yes eof=yes reads=... consumer=diagnostic decrypt=closed keyGate=blocked
```

A successful r17 transport run does **not** imply Spotify playback is unlocked.
It qualifies only sustained ordered encrypted-byte delivery and the bounded consumer
hand-off. The r16 manual AudioKey page remains available for regression diagnostics,
but no additional key retries are added automatically.

Read [DEV2N_R17_CONTINUOUS_AP_STREAM.md](docs/DEV2N_R17_CONTINUOUS_AP_STREAM.md)
for the exact state machine, failure classifications and hardware procedure. The
local fixture test endpoints remain available:

```text
/spotify-test?action=start-vorbis
/spotify-test?action=start-vorbis-chunked
/spotify-test?action=start-vorbis-aes-chunked
/spotify-test?action=stop
```

## Spotify media-key investigation remains open

The second available non-Premium account is **not** a usable AudioKey A/B control: the Spotify app discovers `WLED Matrix` but disables selection with “Passa a Spotify Premium per ascoltare”, so the transfer is blocked before our AP/SPIRC/AudioKey path. Alexa endpoints shown by that account are a different service integration class and are not treated as a protocol model for this usermod.

A separate session-identity audit is documented in `docs/AUDIOKEY_IDENTITY_AUDIT.md`. dev.2n-r14 added AP identity/capability telemetry; r15 audits metadata identity and restrictions without changing the key target or latch. The local AES-CTR stage was hardware-qualified in r10; r11 qualified the producer shape, r12 retained the three encrypted canary ranges in bounded transient memory; r13 additionally verifies the source through the same producer interface while keeping live decrypt/decoder closed.

## Frozen qualified areas

Do not alter without new evidence: shared-I2S/ES8311/DMA, 44.1 kHz PCM ingress, Zeroconf/LoginBlob/persistence, AP DH/Shannon/stored-credential auth, keepalive/Mercury/SPIRC, TrackRef/direct-selection mapping, metadata, dev.2i RequestKey wire/correlation, dev.2l AP StreamChunk transport, and the r14 50 ms AP receive poll.

## Build/test workflow

The source remains an external PlatformIO usermod (`wled-usermod-spotify = symlink://../wled-usermod-spotify`). r1 proved that the dependency declared only inside the symlinked usermod manifest was not visible to the WLED target. r2 added `esphome/micro-vorbis@^0.1.0` explicitly to `[env:waveshare_spotify]`, but the real Arduino target still failed at the same include. That second result is consistent with PlatformIO framework compatibility filtering: upstream micro-vorbis is documented for PlatformIO with `framework = espidf`, while WLED builds this environment with Arduino. r3 therefore keeps the explicit dependency and adds `lib_compat_mode = off` **only to `waveshare_spotify`**, so the package can participate in LDF without changing WLED's framework. See `platformio_override.example.ini`. That historical r3 compatibility change left r1/r2 decoder logic unchanged.

Tests are data-driven through `tests/release_checks.tsv` and `tests/hardware_checks.tsv`; `tools/test_runner.sh` remains generic.

Host/prebuild regression command:

```sh
./tools/test_runner.sh --manifest tests/release_checks.tsv --phase prebuild --repo .
```

The metadata and manual-key native tests compile real C++ with a host C++11 compiler (`g++` by default; `CXX` is supported). Sanitizer testing is opt-in and separate.

The full WLED/PlatformIO target compile is a separate required gate because the current workspace does not contain the complete WLED build tree/toolchain. The dependency compile/link was qualified in r6 and local playback in later hardware tests; this new revision still requires its own target build and diagnostic hardware run.

## Documentation

- `docs/DEV2N_R17_CONTINUOUS_AP_STREAM.md` - current 64 KiB encrypted AP transport diagnostic, bounds and hardware gate.
- `docs/DEV2N_R17_VERIFICATION.md` - host verification, frozen-path checks and target-build limits.
- `docs/DEV2N_R16_MANUAL_AUDIOKEY_PROBE.md` - retained manual AudioKey experiment, budgets, key wiping and hardware evidence.
- `docs/DEV2N_R16_VERIFICATION.md` - actual delivery verification and target-build limits.
- `docs/DEV2N_R15_METADATA_AUDIT.md` - retained metadata parser bounds, field meanings and earlier test sequence.
- `docs/DEV2N_LOCAL_VORBIS.md` — local Ogg/Vorbis decoder design and hardware gate.
- `docs/AUDIOKEY_IDENTITY_AUDIT.md` — frozen current AP identity and comparative AudioKey investigation plan.
- `docs/DEV2M_MEDIA_KEY_BLOCK.md` — qualified media-key service-block hardening and r12-r14 history.
- `docs/DEV2I_AUDIO_KEY.md` — RequestKey/candidate gate and independent librespot evidence.
- `docs/DEV2L_AP_STREAM.md` — qualified bounded sequential AP StreamChunk transport.
- `docs/NEXT_DEV2_CSPOT.md` — staged continuation after the local decoder gate.
- `THIRD_PARTY_NOTICES.md` — decoder/protocol/cryptographic provenance and licensing notes.

### historical dev.2m-r8 opaque context-player discriminator

Hardware r7 settled the transport question: tapping another playlist row generated a second classic SPIRC Load (`load=2`) and another field-19 payload, while classic TrackRef index/GID and metadata remained on the current song. Because the resolver could not identify field 19, the Load was then counted as a duplicate and ACKed with current state. r8 therefore does not assume Dealer-only delivery and does not introduce TLS/WSS. It first fingerprints the opaque payload and searches it only for exact URI/GID identities already present in the retained queue. A unique non-current identity is safe to adopt; all other cases remain non-destructive diagnostics.


### r13 timing telemetry / r14 poll optimization

Hardware r12 qualification on 2026-10-07: first track index 3 (`Mama, I'm Coming Home`) established an 84-entry GID-only queue (`1512 / 84 = 18` bytes/ref). Direct taps then resolved uniquely to index 4 (`Passion`) and index 2 (`Close My Eyes Forever`), with `uidResolved=1`, `queueResolved=1`, `ambiguous=0`, metadata attempts 1->2->3, suppressedTracks 0->1->2 and AP Stream attempts 3->6->9, all 3/3 successful. Shannon remained `macFail=0`.

r13 added `SPIRC timing resolveLast/resolveMax/applyLast/applyMax` in microseconds and metadata `rtt/maxRtt` in milliseconds. Hardware measured two direct taps at `resolveLast=38630/36821 us` and `applyLast=47148/45287 us`; metadata RTT was 1503 ms then 255 ms. Since `apply` begins only once the SPIRC Load is received, the local direct-selection path is only about 45-47 ms and does not explain the perceived ~1 s tap-to-visible-change delay.

r14 therefore changes only `SESSION_POLL_MS` from 250 ms to 50 ms, reducing the local idle receive-check ceiling by 200 ms without changing SPIRC parsing, selection, metadata, RequestKey suppression, StreamChunk or audio semantics. The latency itself is not a release blocker; the r14 hardware gate is primarily to prove no stability, reconnect, FPS or audio-sink regression.



### dev.2n-r9 local AES-128-CTR media-decrypt gate

r9 target compile exposed one integration-only issue before runtime: ESP32-S3 HAL defines `IV_BYTES` as a macro, colliding with the class member of the same name. r10 fixes only that identifier collision with `kIvBytes` / `kKeyBytes`; the crypto byte-stream contract is unchanged.

Hardware r8 qualified the AP-shaped plaintext adapter: 3 chunks, 10,437 supplied bytes, 88,200 decoded frames, zero decoder/feed errors and clean audio. r9 keeps that path byte-for-byte and adds `SpotifyAudioAesCtr`, a sequential mbedTLS AES-128-CTR transform using the fixed legacy Spotify audio IV. The encrypted fixture is generated offline from the same known Ogg file with a synthetic public key, so successful playback proves the decrypt stage preserves exact bytes across 4096-byte boundaries without depending on Spotify's currently blocked AudioKey service.

### dev.2n-r8 4096-byte incremental input gate

Hardware r7 qualified the contiguous local decode path exactly: one deliberate start produced `complete-input-exhausted`, `input=10437/10437`, `frames=88200 expected=88200`, `feedCalls=87`, `feedFail=0`, and a clean speaker output. r8 therefore does not change the codec, PCM sink, task stack, output buffer or Spotify transport. It changes only how the same known fixture becomes visible to the decoder.

The source adapter delivers 4096 + 4096 + 2245 bytes through an 8192-byte staging buffer. Decoder-consumed bytes are removed with bounded compaction while an incomplete tail is retained across refills. This deliberately matches the already-qualified AP StreamChunk range size and proves that Ogg pages/packets crossing a range boundary can be reconstructed before any AES or live Spotify-media integration is attempted.

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
