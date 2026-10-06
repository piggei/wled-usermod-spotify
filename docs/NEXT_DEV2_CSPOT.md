# Current continuation note (dev.2m)

The network/control/media-fetch path is now sufficiently characterized:

- dev.2i: RequestKey works mechanically but the current account/service returns `0x0e / 0:1`; current librespot independently reproduces the same failure.
- dev.2j: ProductInfo reports `headFiles=0`; the legacy media-head URL is unavailable.
- dev.2k: native ESP-IDF TLS cannot link because the prebuilt target framework omits the mbedTLS SSL/TLS engine.
- dev.2l: encrypted media bytes are reachable reliably through AP StreamChunk; three sequential 4 KiB ranges are qualified.
- dev.2m: harden the running receiver around the known keymaster block and suppress redundant RequestKey scans after one exact `0:1` all-candidate result. r3 qualified current SPIRC field-27 TrackRef queue mirroring and restored Android Now Playing; r4 qualified targeted command acknowledgements and Next end-to-end; r5 qualified persistent Play/Pause and Next; r6-r10 progressively identified the direct-row path as classic SPIRC Load field-19, gzip-wrapped 100% printable JSON with `endpoint=play`, but r10 still left the explicit target unresolved. r11 proved `skip_to.track_uid -> Spotify URI` but failed at the classic queue boundary because the observed field-27 queue is GID-only. r12 canonicalized each valid 16-byte GID to `spotify:track:` before matching, kept modern `track_index` advisory only, and was hardware-qualified on repeated direct taps on 2026-10-07. r13 measured the resolved local path at only ~45-47 ms after Load arrival, with metadata RTT varying independently; r14 conservatively reduces only the idle AP receive poll from 250 ms to 50 ms. The tap-to-visible-change delay is not a release blocker, and decoder work may proceed independently after this stability check.

Do not change the qualified shared-I2S/ES8311/DMA/PCM path, LoginBlob credential path, AP handshake/Shannon authentication, persistent Mercury/SPIRC control plane, metadata path, RequestKey wire contract or AP StreamChunk transport without new evidence.

## Next independent workstream: local Ogg/Vorbis decoder qualification

Do not wait for Spotify's media key to start decoder engineering. Use a small, known-good, non-encrypted Ogg/Vorbis fixture stored locally (LittleFS/embedded test asset) and keep Spotify networking out of the experiment.

Required gates:

1. identify a decoder that builds in the actual WLED/ESP32-S3 toolchain without altering the frozen framework globally;
2. decode a bounded local fixture outside the WLED loop;
3. produce signed 16-bit stereo 44.1 kHz PCM;
4. feed only `WavesharePcmOutput::enqueuePcm44100()`;
5. measure internal heap, PSRAM, decoder stack watermark, decode/feed latency and PCM ring high-water;
6. compare the existing synthetic PCM regression before/after decoder integration;
7. no network, AES or AP StreamChunk coupling in the first decoder gate.

## Following gate: streaming decoder producer

After local fixture decode is stable, replace whole-file assumptions with a bounded producer/ring interface that can accept arbitrary media chunks while retaining the same PCM output API. Exercise it with the same local fixture before attaching Spotify bytes.

## Deferred gate: AES decrypt integration

Only when Spotify again supplies a valid 16-byte media key for an account/session should the real chain be joined:

```text
AP StreamChunk encrypted bytes
        -> media AES decrypt
        -> Ogg/Vorbis streaming decoder
        -> signed 16-bit stereo 44.1 kHz
        -> WavesharePcmOutput::enqueuePcm44100()
```

Until then, do not invent alternate key derivation or weaken protocol/security checks.

## Separate hardening observation

A previous run received a SPIRC Load without a selected GID after an earlier valid Load. Keep empty/transition Load handling as a separate SPIRC hardening item; it is not evidence about the media-key failure.
