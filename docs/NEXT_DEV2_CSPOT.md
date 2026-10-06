# Current continuation note (dev.2l)

The old ProductInfo `head-files-url` route is unavailable (`headFiles=0`), and native ESP-IDF TLS cannot link because the target framework has no mbedTLS TLS engine. Before considering a private TLS stack or a globally rebuilt framework, dev.2l performs a bounded AP StreamChunk reachability test over the already-qualified Shannon session. CDN/Login5/storage-resolve remain deferred until this result is known.

# Current development sequence after dev.2j-r2

Do not change the qualified shared-I2S/ES8311/DMA/PCM path, LoginBlob credential path, AP handshake/Shannon authentication, persistent Mercury/SPIRC control plane, dev.2h metadata path or dev.2i RequestKey wire contract unless new evidence demonstrates a regression.

## Closed diagnostic question: RequestKey vs account/service

The 2026-10-06 dev.2i-r2 hardware run tried all four AudioFile candidates for one track and received `0x0e / 0:1` for every format (`1`, `2`, `0`, `8`) with no protocol error or timeout.

A separate current librespot 0.8.0 Windows build, same account and network, independently authenticated and returned `audio key 0 1` on two normal tracks. It also reported the same `Country: "NG"` received by the ESP32.

Conclusion: preserve RequestKey. The valid-key success gate remains open, but rewriting dev.2i without new evidence is no longer justified.

## Closed diagnostic question: ProductInfo media-head path

dev.2j-r2 proved on hardware that ProductInfo is valid XML-like data and reports:

```text
type=premium catalogue=premium playerLicense=premium headFiles=0
headUrl=no
```

Therefore the legacy `head-files-url` canary is unavailable for this account/session. Do not spend another build trying looser XML parsing or guessing a URL.

## Current gate: dev.2k-r1 target-native HTTPS + spclient resolver

Use native ESP-IDF `esp_http_client` with the certificate bundle to query the current Spotify resolver over verified HTTPS and extract one `spclient` endpoint. Keep the response bounded and expose only status/counters/endpoint.

Required evidence:

1. build links against the actual WLED target toolchain;
2. certificate bundle is enabled and verification is not bypassed;
3. HTTPS resolver returns status 200;
4. bounded response contains a `spclient` endpoint;
5. AP/Shannon/SPIRC/audio baseline remains healthy.

## Next gate after TLS PASS: client-token + Login5 bearer

Current public clients attach an optional `Client-Token` and always attach `Authorization: Bearer <access token>` to spclient calls. The access token is obtained through Login5. Implement this as its own gate:

1. resolve/reuse the qualified spclient endpoint;
2. obtain a client token using the current public protocol contract, but never serialize the token;
3. obtain a Login5 access token from the stored Spotify credential, again with length/status-only diagnostics;
4. perform a harmless authenticated spclient request before attempting media storage resolution;
5. handle 401 by one bounded token refresh rather than uncontrolled retries.

## Following gate: storage-resolve

For one selected AudioFile use the current versioned route:

```text
/storage-resolve/v2/files/audio/interactive/<format>/<fileId>
```

Parse only enough `StorageResolveResponse` to classify the result and locate a CDN URL. Keep URL query/auth material out of `/json/info`.

## Following gate: bounded CDN range

With a valid storage response, request only a small CDN range and record status/content-range/byte count. Do not decrypt or decode in this gate.

## Following gate: decrypt/container validation

Only after a valid 16-byte audio key is obtainable for an account/session, apply the selected media decryption and prove that decrypted bytes form the expected container. Keep decoder and DAC disconnected.

## Following gate: decoder -> qualified PCM sink

Only after container validation:

```text
media bytes -> decrypt -> decoder
          -> 44.1 kHz signed 16-bit stereo
          -> WavesharePcmOutput::enqueuePcm44100()
          -> qualified shared-I2S / ES8311
```

Network/decode work must stay outside the WLED loop and record stack, internal heap, PSRAM, feed latency and PCM ring high-water. Run the existing synthetic 44.1 kHz PCM regression before and after integration.

## Separate hardening observation

A dev.2j-r2 run later received a SPIRC Load with no selected GID and set `lastError=SPIRC Load selected track has no GID` after an earlier valid Load had already produced correct metadata. Keep this as a separate SPIRC track-change/empty-load hardening item; do not conflate it with TLS/media work.
