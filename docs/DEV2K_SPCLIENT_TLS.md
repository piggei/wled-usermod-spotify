# dev.2k-r1 native TLS / spclient resolver gate

## Starting evidence

dev.2j-r2 closed the ProductInfo ambiguity on hardware. A real 3399-byte XML-like ProductInfo packet was decoded with:

```text
type=premium
catalogue=premium
playerLicense=premium
headFiles=0
headUrl=no
hash=0x4f1d312e
```

After transfer of a normal track, the four bounded AudioFile candidates again returned `AesKeyError 0x0e / 0:1`, while Shannon remained `macFail=0` and the frozen audio backend remained clean. Therefore the legacy ProductInfo `head-files-url` route is not available for this account/session and development moves to the current spclient path.

A later SPIRC frame in the same run reported `SPIRC Load selected track has no GID`; the previously resolved track metadata remained intact. That is recorded as a separate control-plane hardening observation and is not mixed into this TLS gate.

## Purpose

Prove two facts on the real WLED target before implementing Spotify HTTP authentication:

1. the external usermod can call the native ESP-IDF HTTPS stack;
2. certificate-verified `apresolve.spotify.com` returns a current `spclient` endpoint.

This gate must not obtain or log access tokens, client tokens, storage responses or media bytes.

## Implementation

- Keep the existing qualified plain-TCP AP session unchanged.
- Use `esp_http_client` only for this new HTTPS canary.
- Use the ESP-IDF certificate bundle through `esp_crt_bundle_attach` when the build exposes `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE`.
- Never set an insecure/skip-verification TLS option.
- Request the current resolver URL with `accesspoint`, `dealer` and `spclient` types.
- Capture at most 2048 response bytes.
- Require HTTP 200 and a non-truncated response.
- Extract only the first `spclient` host:port from JSON.
- Execute at most once per live AP session after the bounded AudioKey candidate scan reaches a terminal result.

The operation remains in the dedicated Spotify session task, not in the WLED main loop.

## Telemetry

```text
SpClient TLS attempts=<n> ok=<n> skipped=<n> http=<status> bytes=<n> truncated=<yes|no> bundle=<yes|no> endpoint=<host:port|none>
SpClient TLS lastError=<text>
```

The endpoint hostname is not credential material. The resolver body itself is not exposed.

## PASS criteria

```text
attempts>=1
ok>=1
skipped=0
http=200
bytes>0
truncated=no
bundle=yes
endpoint!=none
lastError=none
```

In parallel:

```text
Shannon macFail=0
reconnect attempts=0 unless externally induced
I2S err=0 short=0 late=0 ringUnderrun=0
```

## Classified non-PASS results

`esp_http_client unavailable in target headers` means the chosen native API is not exposed by the actual WLED build environment.

`ESP-IDF certificate bundle unavailable` means the API is present but no qualified trust store is enabled. Do not work around this by disabling certificate verification.

`verified HTTPS apresolve failed` means headers/linking succeeded but TLS/network execution failed; collect compile/runtime evidence before changing architecture.

`HTTPS apresolve missing spclient endpoint` means HTTPS succeeded but the response contract differs and the bounded JSON extraction must be revisited.

## Deliberately out of scope

- Login5 request/response;
- client-token acquisition/challenges;
- `Authorization: Bearer` and `Client-Token` headers;
- storage-resolve protobuf;
- CDN URL use or range fetch;
- media AES decrypt;
- OGG/Vorbis decode;
- PCM playback from Spotify.

## Hardware result - 2026-10-06

**CLOSED / TARGET TLS UNAVAILABLE.** The real `waveshare_spotify` PlatformIO build reached final ELF linking, where `libesp-tls.a` and the certificate-bundle object referenced `mbedtls_ssl_init`, `mbedtls_ssl_read`, `mbedtls_ssl_write`, `mbedtls_ssl_handshake`, `mbedtls_ssl_conf_ca_chain` and related functions, but none was defined. Direct `xtensa-esp-elf-nm` inspection found no `mbedtls_ssl_init` definition in any ESP32-S3 framework `.a`; `libmbedtls.a` contains crypto/bundle objects but no `ssl_tls`, `ssl_msg` or `ssl_ciphersuites` objects.

Conclusion: **native TLS unavailable** in this prebuilt target framework. The failure is not a static-library order problem and is not fixed by `--whole-archive`. Do not disable certificate verification and do not globally rebuild WLED solely for this usermod without a separate qualification campaign. Active development continues with `dev.2l` AP StreamChunk.
