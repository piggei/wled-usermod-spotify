# WLED Spotify Connect Usermod

Current development build: **v0.1.0-dev.2l-ap-stream-r1**

Target baseline: WLED 17.0.0-devV5 / `ESP32-S3_Waveshare_HUB75` on the Waveshare ESP32-S3 RGB Matrix board.

## Current status

The qualified control plane remains intact through Zeroconf/LoginBlob, AP handshake/DH/Shannon authentication, persistent Mercury/SPIRC, TrackRef extraction and Mercury track metadata. The Waveshare shared-I2S/ES8311 backend and 44.1 kHz PCM ingress remain frozen.

The dev.2i gate is technically validated but service-blocked for the current account: all four AudioFile candidates returned correlated `AesKeyError 0x0e / 0:1`. A separate current librespot 0.8.0 build on native Windows, using the same account/network, independently authenticated and returned the same `audio key 0 1` on multiple normal tracks. RequestKey is therefore not being rewritten without new evidence.

## Closed prerequisite investigations

`dev.2j-r2` proved from real ProductInfo that this session is premium but has `headFiles=0` and no `head-files-url`; the legacy ProductInfo media-head path is unavailable.

`dev.2k-r1` then tested native ESP-IDF HTTPS. The real `waveshare_spotify` build reached final linking but failed on undefined `mbedtls_ssl_*` symbols. Symbol inspection found no definition of `mbedtls_ssl_init` in any ESP32-S3 framework archive and no SSL/TLS objects in `libmbedtls.a`. The prebuilt WLED/Tasmota framework contains crypto primitives but not the TLS engine needed by `esp_http_client`/`esp-tls`. This is classified target evidence, not a reason to weaken certificate verification or patch global WLED linkage.

## Scope of dev.2l-r1

This build tests an independent transport already available inside the authenticated Spotify Access Point session: the historical AP **StreamChunk** channel. After the bounded AudioKey candidate scan reaches a terminal result, the firmware sends one command `0x08` for the preferred AudioFile and requests only **4096 bytes** (1024 protocol words) starting at offset zero. Responses `0x09` and channel errors `0x0a` are correlated by a 16-bit channel id.

The request follows the independently reimplemented historical wire contract:

```text
channelId BE16
00 01
0000 BE16
00000000 BE32
00009c40 BE32
00020000 BE32
fileId[20]
offsetWords BE32
endWords BE32
```

Total request payload: **46 bytes**. Media response bytes are never retained as a file or exposed through JSON; the canary only parses channel framing, counts headers/data, records historical header `0x03` file-size information when present, and discards data immediately.

No TLS, spclient/Login5, CDN, AES decrypt, Vorbis decoder or PCM connection is added in this build.

## Expected `/json/info` additions

```text
AP Stream attempts=<n> ok=<n> failures=<n> timeouts=<n> protoErr=<n> stale=<n> trackCancel=<n> pending=<yes|no>
AP Stream channel=<id> requestBytes=46 requested=4096 responsePackets=<n> lastCmd=0x<cmd> failureCode=<n>
AP Stream headers=<n> headerBytes=<n> headerDone=<yes|no> fileBytes=<n> dataPackets=<n> dataBytes=<n> format=<metadata format>
AP Stream lastError=<text>
```

A reachability PASS is `ok>=1` with `dataBytes>0`. A `0x0a` response is also a useful classified result and should report `failures>=1`, `lastCmd=0xa` and a channel failure code. A timeout remains distinct from a protocol/framing error.

## Frozen qualified baseline

Do not alter without new evidence: shared-I2S/ES8311/DMA, Zeroconf/LoginBlob/persistence, AP DH/Shannon/stored-credential auth, keepalive/Mercury/SPIRC, TrackRef/metadata, and the dev.2i RequestKey wire contract/candidate correlation. Future decoded PCM must still enter only through `WavesharePcmOutput::enqueuePcm44100()`.

## Build/test workflow

The source remains an external PlatformIO usermod (`wled-usermod-spotify = symlink://../wled-usermod-spotify`). Tests are data-driven through `tests/release_checks.tsv` and `tests/hardware_checks.tsv`; `tools/test_runner.sh` remains generic. Full WLED/PlatformIO compile and hardware behavior are qualification steps on the target machine.

Manual controls remain `/spotify-session?action=stop|reset|probe` and `/spotify-test?action=start-pcm&tone=1000`.

## Documentation

- `docs/DEV2I_AUDIO_KEY.md` — RequestKey/candidate gate and independent librespot evidence.
- `docs/DEV2J_MEDIA_HEAD.md` — ProductInfo investigation and confirmed `headFiles=0`.
- `docs/DEV2K_SPCLIENT_TLS.md` — closed target-native TLS diagnostic.
- `docs/DEV2L_AP_STREAM.md` — current bounded AP StreamChunk gate.
- `docs/NEXT_DEV2_CSPOT.md` — staged future media/decode work.
- `THIRD_PARTY_NOTICES.md` — protocol/cryptographic provenance and licensing notes.
