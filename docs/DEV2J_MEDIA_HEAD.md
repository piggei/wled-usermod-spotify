# dev.2j-r2 ProductInfo / media-head access canary

## Starting evidence

dev.2i-r2 is retained unchanged. Real hardware tested four distinct metadata AudioFile candidates (formats `1`, `2`, `0`, `8`) and Spotify returned the same correlated `AesKeyError 0x0e / 0:1` for every candidate. `protoErr=0`, `timeouts=0`, `stale=0`, Shannon `macFail=0` and the frozen audio backend remained clean.

An independent current librespot 0.8.0 Windows build using the same Spotify account/network then authenticated successfully, reported `Country: "NG"`, loaded two normal music tracks and independently returned `audio key 0 1`. Therefore this revision does not rewrite RequestKey or the qualified AP crypto/control plane merely to work around an externally reproduced keymaster failure.

## r1 hardware result

The first hardware run received a real ProductInfo packet (`packets=1`, `bytes=3399`) but the exact-tag parser reported `headUrl=no`. After the track transfer, the four AudioKey candidates again ended in correlated `0x0e / 0:1`; the media-head canary therefore stopped with `ProductInfo head-files-url missing`. Session, Shannon and the frozen audio backend remained healthy.

This does **not** prove that the AP packet is malformed. Current librespot parses ProductInfo as UTF-8 XML with a general XML parser, while r1 searched for one literal `<head-files-url>` opening tag. r2 first tightens ProductInfo diagnostics before adding TLS/storage-resolve complexity.

## Purpose

Determine whether `head-files-url` is actually absent from the account ProductInfo delivered to this receiver or was merely missed by the r1 literal-tag scanner. When present, retain the same bounded 4 KiB media-head canary.

Public current librespot receives ProductInfo as XML and commonly exposes a `head-files-url` account attribute. dev.2j-r2 treats that path only as a bounded network/container canary.

## Wire/runtime behavior

- AP live command `0x50` is recognized as ProductInfo.
- The XML payload is scanned with a bounded tag extractor that accepts both `<tag>` and `<tag attr=...>` opening forms, matching the semantics of a real XML parser closely enough for selected scalar fields.
- r2 records only a non-sensitive ProductInfo synopsis: `type`, `catalogue`, `player-license`, `head-files`, XML-likeness and a 32-bit FNV-1a payload fingerprint.
- No full ProductInfo body is logged.
- The full template is retained in RAM only for the active session and is not exposed in `/json/info`.
- After the current audio-key scan reaches terminal success, all-candidate `0x0e` rejection, or all-candidate timeout, candidate `0` (the same preferred AudioFile used first by dev.2i-r2) is selected for the canary.
- `{file_id}` is replaced by the lowercase 40-hex AudioFile id.
- Only a plain `http://` template is executed in r1.
- One request adds `Range: bytes=0-4095` and consumes at most 4096 response bytes.
- HTTP 200 or 206 is accepted as a transport success; `range=yes` specifically means 206.
- The first four bytes are checked for `OggS` and immediately discarded with the rest of the sampled body.

The AP worker may block for at most the existing dedicated media-head timeout (`5000 ms`) while performing this canary. No network/decode work is moved into the WLED main loop.

## HTTPS classification

Historical/current Spotify ProductInfo examples show both plain-HTTP and HTTPS `head-files-url` templates. The target handoff explicitly warns that `WiFiClientSecure.h` / `NetworkClientSecure.h` were not exposed to this external WLED module in the qualified toolchain.

Therefore r1 does not guess at a secure Arduino-client integration. For an HTTPS template it records:

```text
scheme=https
MediaHead skipped>=1
unsupportedScheme>=1
MediaHead lastError=HTTPS head-files-url unsupported by this gate
```

This result is expected and opens a dedicated TLS/API investigation; it is not a reason to modify the shared-I2S/audio backend.

## Telemetry

```text
ProductInfo packets=<n> bytes=<n> hash=0x<fnv32> xml=<yes|no> headUrl=<yes|no> scheme=<http|https|other|none>
ProductInfo attrs type=<value> catalogue=<value> playerLicense=<value> headFiles=<value>
MediaHead attempts=<n> ok=<n> skipped=<n> http=<status> len=<length> bytes=<0..4096> range=<yes|no> ogg=<yes|no> unsupportedScheme=<n>
MediaHead lastError=<text>
```

Security/privacy constraints:

- no head-file URL/template in JSON;
- no full ProductInfo XML/body in JSON;
- no extra AudioFile id in the media-head telemetry;
- no fetched media bytes in logs;
- no AES key in logs;
- no username/authData/token material added.

## Hardware qualification

1. Flash `v0.1.0-dev.2j-media-head-r2`.
2. Confirm the stored credential restores and the AP session reaches SPIRC-ready.
3. Transfer a normal music track from the official Spotify app.
4. Wait until the bounded dev.2i candidate scan is terminal.
5. Capture `/json/info`.
6. Classify ProductInfo/head-file result using the README rules.
7. Run `/spotify-test?action=start-pcm&tone=1000` for at least 30 seconds and confirm the frozen audio counters remain clean.

Do not require a valid AES key for this canary. The point of dev.2j-r2 is to separate media-file reachability from the independently reproduced keymaster rejection.

## Not implemented

- HTTPS head-file access;
- Login5 access token;
- client-token request/challenge handling;
- spclient resolver/authenticated requests;
- `storage-resolve` protobuf parsing;
- CDN range streaming;
- AES media decrypt;
- OGG/Vorbis or other decode;
- PCM feed from Spotify.

## r2 hardware result and closure

The r2 parser was exercised on hardware on 2026-10-06. The same 3399-byte ProductInfo payload is XML-like and decodes as:

```text
hash=0x4f1d312e
xml=yes
headUrl=no
scheme=none
type=premium
catalogue=premium
playerLicense=premium
headFiles=0
```

After transfer of `Beat Of Your Heart - Club Dub Edit`, metadata remained valid and the four AudioFile candidates again returned correlated `0x0e / 0:1`. The media-head step was skipped with `ProductInfo head-files-url missing`; Shannon and audio diagnostics remained healthy.

Conclusion: the legacy head-file path is **not available for this account/session**. This gate is closed as a classified `NOT AVAILABLE`, not as a parser failure. Development moves to dev.2k native HTTPS/spclient resolution. Do not add another ProductInfo parser revision unless new service evidence shows `head-files-url` is actually present.
