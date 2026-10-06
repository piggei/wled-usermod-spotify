# Third-party notices and protocol references

## Waveshare / ES8311 audio initialization

The ES8311 playback initialization in this package is adapted from the
Waveshare ESP32-S3 RGB Matrix example code and from the Apache-2.0 ES8311 path
previously used by the WLED Buzzer Usermod. Preserve the applicable upstream
Apache-2.0 notices when redistributing derived code.

## Shannon stream cipher

`spotify/SpotifyShannon.*` is an independent C++ implementation of the Shannon
stream-cipher primitive from the public design/specification by Philip Hawkes,
Gregory G. Rose and related authors. The Shannon publication states that the
cipher and its reference source may be used freely for any purpose. No cspot
Shannon source is bundled in this package.

The host regression in `tests/test_shannon_vector.py` is cross-checked against
the public example vector published by `twonky4/shannon`, an MIT-licensed
JavaScript implementation. The test uses only the documented key/plaintext and
expected ciphertext/MAC values; it does not vendor that JavaScript source.

References used during interoperability work:

- *Design and Primitive Specification for Shannon*, ePrint 2007/044.
- `twonky4/shannon` public repository / npm package (`shannon` 0.0.1, MIT).

## Spotify AP protocol / cspot interoperability reference

The AP wire flow and protobuf field mapping were checked against publicly
available Spotify Connect interoperability implementations and protocol
schemas, including cspot, but this package does **not** vendor or link cspot or
bell source code.

Important licensing note: the currently published `feelfreelinux/cspot`
repository is GPL-3.0-or-later. Do not describe cspot as MIT and do not copy or
vendor cspot implementation source into this module without first making an
explicit licensing decision for the combined firmware.

This module therefore contains a small local AP/Mercury/SPIRC wire encoder/decoder and an
independent Shannon primitive rather than importing the cspot runtime.

## Audio-key wire reference

The dev.2i AP `RequestKey` payload and sequence-correlated `AesKey` / `AesKeyError`
handling were checked against the public current librespot audio-key manager
contract. This package implements that small wire contract independently; no
librespot source code is copied, vendored or linked.

dev.2i-r2 does not change that wire contract. It only keeps a bounded local
inventory of metadata AudioFile candidates and retries the independent request
against the next candidate after a correlated service error/timeout.

## ProductInfo / media-head interoperability reference

The dev.2j media-head canary was checked against public current librespot behavior:
AP ProductInfo may provide a `head-files-url` account attribute, and librespot
uses that attribute to request the first 128 KiB of an AudioFile unencrypted.
This package independently parses only that single ProductInfo attribute and
performs a much smaller bounded 4 KiB canary request. No librespot source code
is copied, vendored or linked.

The later authenticated storage-resolution design notes are based on public
interoperability behavior only. dev.2j-r2 does not implement Login5, client-token,
spclient storage resolution, CDN download, media decryption or codec decoding.

## Native TLS / spclient resolver interoperability reference

The dev.2k resolver gate is based on publicly observable current-client behavior:
modern librespot resolves `accesspoint`, `dealer` and `spclient` through
`https://apresolve.spotify.com/`, and current open-source Spotify Connect clients
perform authenticated spclient calls over HTTPS. This module independently uses
the ESP-IDF HTTP client and certificate bundle only to perform a bounded resolver
canary. No librespot/go-librespot source code is copied, vendored or linked.

The later design note that spclient requests carry a Login5 bearer token and may
also carry a client token, and that interactive audio storage uses the versioned
`/storage-resolve/v2/files/audio/interactive/<format>/<fileId>` route, is retained
as interoperability research for future gated implementation. dev.2k-r1 does not
implement any of those authentication or storage operations.

## AP StreamChunk protocol provenance (dev.2l)

The dev.2l StreamChunk code is an independent C++ implementation of protocol facts cross-checked against historical librespot source and public interoperability reports: request command `0x08`, response `0x09`, channel error `0x0a`, 16-bit channel correlation, 46-byte request layout and the length-prefixed channel-header framing. No librespot or cspot source code is copied into this repository. Preserve librespot's upstream licensing notices when consulting or redistributing upstream material; do not vendor GPL-3.0-or-later cspot code into this project without an explicit licensing decision.
