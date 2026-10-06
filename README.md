# WLED Spotify Connect Usermod

Current development build: **v0.1.0-dev.2m-key-block-r14**

**r14 is a conservative scheduling-only optimization above the hardware-qualified r12 canonical queue selection gate and the hardware-qualified r13 timing measurement.** r12 successfully resolved direct playlist taps through `skip_to.track_uid -> URI -> canonical GID-only classic TrackRef`, and repeated taps changed TrackRef/GID/metadata/AP Stream correctly. r13 deliberately preserves that control-plane behavior and adds only latency telemetry to localize the observed roughly one-second UI delay before any polling/scheduling optimization. r8a hardware proved that the current Android `Frame.context_player_state` is a gzip member (`1f8b08...`), not an uncompressed JSON/protobuf blob. r9 therefore parses the gzip envelope, enforces a 128 KiB decompressed-size ceiling, inflates the raw DEFLATE member through the ESP32-S3 ROM miniz helper, verifies the gzip CRC32, then runs the existing bounded JSON/protobuf target parser and exact retained-queue correlation on the decompressed bytes. The decompressed body exists only for the current frame and is never serialized.

r3 fixed the current-SPIRC queue/state encoding and made Android Now Playing remain visible. r4 qualified classic command acknowledgements and Next end-to-end. r5 qualified persistent Play/Pause, Next, metadata refresh, media-key suppression and a second AP Stream probe. Direct playlist taps remained the only open classic-control issue. r6 rejected the historical JSON assumption for `Frame.context_player_state`; r7 then proved on hardware that a direct Android row tap **does reach the classic SPIRC bus as a second Load**, but the top-level State still names the current track while field 19 changes from the small initial payload to a much larger opaque binary payload. The old duplicate-Load guard therefore discarded the real selection. r8 characterizes that opaque field safely and correlates exact URI/GID identities against the already-retained field-27 queue, selecting only when exactly one non-current queue entry is unambiguous.

Target baseline: WLED 17.0.0-devV5 / `ESP32-S3_Waveshare_HUB75` on the Waveshare ESP32-S3 RGB Matrix board.

## Current status

The qualified path is preserved through Zeroconf/LoginBlob, AP handshake/DH/Shannon authentication, persistent Mercury/SPIRC, TrackRef, Mercury metadata, AudioFile selection and encrypted media transport over AP StreamChunk. The Waveshare shared-I2S/ES8311 backend and `enqueuePcm44100()` ingress remain frozen.

`dev.2l-r2` is qualified on real hardware: three consecutive encrypted 4096-byte ranges at offsets 0/4096/8192 completed with `attempts=3 ok=3`, `dataBytes=12288`, no timeout/protocol/stale errors, `macFail=0`, and no audio-backend regression.

The remaining external blocker is the media AES key. On the affected Premium account all four AudioFile candidates return correlated `AesKeyError 0x0e / 0:1`. Current librespot 0.8.0 on native Windows, same account/network, independently reproduced `audio key 0 1` on multiple tracks and the same `Country: "NG"`. RequestKey is therefore frozen pending new evidence.

Hardware r10 kept the gzip decoder stable and the controller usable, but direct selection still remained unresolved. On the first transfer the decoded JSON was 1,412 bytes with `endpoint=play`; after a direct row tap the second classic Load inflated to a 20,634-byte last decoded JSON body, while TrackRef/metadata remained unchanged and `unresolved` advanced 1->2. r11 then proved the modern side of the mapping: the direct tap produced one bounded `skip_to`, found its `track_uid`, and resolved that UID to a Spotify track URI (`uidResolved=1`), but `queueResolved` stayed zero. The same session retained 82 classic TrackRefs occupying 1,476 bytes total: exactly 18 bytes per TrackRef, the protobuf size of field 1 + length 16 + a 16-byte GID. r12 therefore canonicalizes those GID-only classic TrackRefs to `spotify:track:` URIs before matching the modern UID-resolved URI.

## Scope of dev.2m-r14

This build hardens the receiver around that known service-side block instead of retrying it on every track.

- A freshly started Spotify session performs the existing bounded candidate scan once.
- The block is latched **only** when every candidate ends in `0x0e / 0:1`. Other error codes/timeouts do not activate the latch.
- While latched, subsequent tracks in the same started session still perform SPIRC/TrackRef/metadata processing but suppress redundant RequestKey scans.
- The already-qualified AP StreamChunk canary may still run for each new track, proving encrypted media reachability remains alive.
- Automatic AP reconnects inside the same session task preserve the latch, avoiding request storms. A fresh usermod/session start clears it so service recovery can be detected.
- No AES key, credential, media body or reusable token is exposed. No decrypt, decoder or PCM feed is introduced.
- The r3 field-27 queue mirror remains unchanged; real hardware proved it keeps Android Now Playing visible.
- r4 command/ack handling remains intact and is hardware-qualified for Next.
- r5 decouples controller playback state from AES-key availability: a missing key keeps audio silent but no longer forces the Spotify UI back to Pause.
- r5 keeps a small virtual playback-position clock so Play/Pause/Seek Notify state remains temporally coherent even while no PCM is produced.
- r5 also accepts direct queue selection carried inside a Play frame through `playing_track_index`/State index, resolving that index against the retained field-27 TrackRef queue before requesting metadata.
- Empty/duplicate Load retries remain actively acknowledged with the retained state.
- r7 stops advertising `kSupportsPlaylistV2`; classic commands/field-27 queue remain supported, but the newer playlist-v2/connect-state command contract is not claimed.
- r7 hardware proved that direct row selection is still delivered through classic SPIRC: `load` advanced 1→2 and field-19 traffic changed, while the top-level selected GID/index remained the old track. The payload did not match the historical JSON or the initially assumed current protobuf identity shape.
- r8 keeps both schema-specific parsers as bounded compatibility paths but adds a schema-agnostic diagnostic envelope: last payload length, FNV-1a identity hash, first 16 bytes as hex, compression/magic classification, printable ratio and top-level protobuf wire summary. No raw field-19 body is exposed.
- r8a hardware established the missing envelope exactly: field 19 is gzip. The current-track payload ended with `lastBytes=2259`, while a direct row tap produced a new `lastBytes=6332` gzip member; raw URI/GID scanning correctly found nothing because the selection was compressed.
- r9a parses that gzip envelope with the ESP32-S3 ROM miniz `tinfl` path. The high-level `tinfl_decompress_mem_to_mem()` helper from r9 is intentionally not used because it places the large `tinfl_decompressor` state on the caller stack; r9a heap-allocates that state and calls low-level `tinfl_decompress()` instead. `ISIZE` remains bounded to 1..131072 bytes, CRC32 is verified, output prefers PSRAM, and all transient buffers are released immediately.
- r9 first parses the inflated body for an explicit historical `skip_to`/URI/index target; if no schema target is available it applies the same exact queue-identity scan to the inflated bytes. A target is adopted only when it resolves to a non-current retained TrackRef.
- r12 preserves the bounded r11 multi-`skip_to` enumeration but canonicalizes each retained TrackRef identity: a native `spotify:track:` URI is preferred, otherwise a valid 16-byte GID is converted with the already-qualified `spotifyTrackUriFromGid()` path.
- `track_index` inside modern context JSON is advisory only. It is counted as validated only when it equals the URI/UID-resolved classic queue index; index-only or page-relative-looking values are ignored rather than used to select a song.

## `/json/info` additions

```text
MediaKey state=<idle|requesting|diagnostic|ready|service-blocked> blocked=<yes|no> blockEvents=<n> suppressedTracks=<n> blockErr=<a>:<b>
```

Expected first-track result on the currently affected account:

```text
AudioKey requests=4 responses=4 ok=0 errors=4 ...
MediaKey state=service-blocked blocked=yes blockEvents=1 suppressedTracks=0 blockErr=0:1
```

The r12 direct-selection gate and r13 timing measurement are hardware-qualified. The immediate r14 gate is stability-first after reducing only the idle AP receive poll from 250 ms to 50 ms: After the first blocked track, tap exactly one different row in the same playlist and inspect `SPIRC contextInflate`. Expected first success is `attempts>=1 ok>=1 failures=0 status=ok`. For the observed GID-only queue, `SPIRC contextQueue` should report `gidOnly>0` and `canonical=refs`. A direct tap should then produce `contextSkip uidResolved>=1 queueResolved>=1`, a unique non-current index, `bySkip>=1`/`byInflate>=1`, new TrackRef/GID/metadata, unchanged RequestKey count under the service-block latch, and another AP Stream 3/3. `track_index` may validate the identity but must never select by itself.

## Closed prerequisite investigations

- `dev.2j-r2`: ProductInfo is valid Premium data but reports `headFiles=0`; no legacy `head-files-url` is supplied.
- `dev.2k-r1`: native ESP-IDF TLS reaches link stage but the prebuilt target framework has no mbedTLS SSL/TLS engine (`mbedtls_ssl_*` definitions absent). Do not weaken certificate verification or globally rebuild WLED for this gate.
- `dev.2l-r2`: encrypted media range transport through the existing AP/Shannon connection is qualified.

## Frozen qualified baseline

Do not alter without new evidence: shared-I2S/ES8311/DMA, 44.1 kHz PCM ingress, Zeroconf/LoginBlob/persistence, AP DH/Shannon/stored-credential auth, keepalive/Mercury/SPIRC, TrackRef/metadata, dev.2i RequestKey wire contract/correlation, and dev.2l AP StreamChunk transport.

The next independent workstream after dev.2m is local Ogg/Vorbis decode qualification feeding only `WavesharePcmOutput::enqueuePcm44100()`. It must not depend on a live Spotify AES key.

## Build/test workflow

The source remains an external PlatformIO usermod (`wled-usermod-spotify = symlink://../wled-usermod-spotify`). Tests are data-driven through `tests/release_checks.tsv` and `tests/hardware_checks.tsv`; `tools/test_runner.sh` remains generic. Full WLED/PlatformIO compile and hardware behavior remain qualification steps on the target machine.

Manual controls remain `/spotify-session?action=stop|reset|probe` and `/spotify-test?action=start-pcm&tone=1000`.

## Documentation

- `docs/DEV2I_AUDIO_KEY.md` — RequestKey/candidate gate and independent librespot evidence.
- `docs/DEV2J_MEDIA_HEAD.md` — ProductInfo investigation and confirmed `headFiles=0`.
- `docs/DEV2K_SPCLIENT_TLS.md` — closed target-native TLS diagnostic.
- `docs/DEV2L_AP_STREAM.md` — qualified bounded sequential AP StreamChunk transport.
- `docs/DEV2M_MEDIA_KEY_BLOCK.md` — current service-block hardening gate.
- `docs/NEXT_DEV2_CSPOT.md` — staged local decoder/decrypt continuation.
- `THIRD_PARTY_NOTICES.md` — protocol/cryptographic provenance and licensing notes.


### r8 opaque context-player discriminator

Hardware r7 settled the transport question: tapping another playlist row generated a second classic SPIRC Load (`load=2`) and another field-19 payload, while classic TrackRef index/GID and metadata remained on the current song. Because the resolver could not identify field 19, the Load was then counted as a duplicate and ACKed with current state. r8 therefore does not assume Dealer-only delivery and does not introduce TLS/WSS. It first fingerprints the opaque payload and searches it only for exact URI/GID identities already present in the retained queue. A unique non-current identity is safe to adopt; all other cases remain non-destructive diagnostics.


### r13 timing telemetry / r14 poll optimization

Hardware r12 qualification on 2026-10-07: first track index 3 (`Mama, I'm Coming Home`) established an 84-entry GID-only queue (`1512 / 84 = 18` bytes/ref). Direct taps then resolved uniquely to index 4 (`Passion`) and index 2 (`Close My Eyes Forever`), with `uidResolved=1`, `queueResolved=1`, `ambiguous=0`, metadata attempts 1->2->3, suppressedTracks 0->1->2 and AP Stream attempts 3->6->9, all 3/3 successful. Shannon remained `macFail=0`.

r13 added `SPIRC timing resolveLast/resolveMax/applyLast/applyMax` in microseconds and metadata `rtt/maxRtt` in milliseconds. Hardware measured two direct taps at `resolveLast=38630/36821 us` and `applyLast=47148/45287 us`; metadata RTT was 1503 ms then 255 ms. Since `apply` begins only once the SPIRC Load is received, the local direct-selection path is only about 45-47 ms and does not explain the perceived ~1 s tap-to-visible-change delay.

r14 therefore changes only `SESSION_POLL_MS` from 250 ms to 50 ms, reducing the local idle receive-check ceiling by 200 ms without changing SPIRC parsing, selection, metadata, RequestKey suppression, StreamChunk or audio semantics. The latency itself is not a release blocker; the r14 hardware gate is primarily to prove no stability, reconnect, FPS or audio-sink regression.
