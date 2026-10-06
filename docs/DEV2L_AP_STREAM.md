# dev.2l - bounded AP StreamChunk canary

## Purpose

Test whether encrypted AudioFile bytes can still be obtained through the already authenticated Access Point/Shannon session, independently of both the account-specific AudioKey failure and the target framework's unavailable TLS engine.

## Protocol basis

Historical librespot implementations used AP command `0x08` to request an audio range. The response channel is identified by a big-endian `u16`; AP command `0x09` carries headers/data and `0x0a` is a channel error. The historical request payload is 46 bytes and carries fixed protocol fields, the 20-byte AudioFile id, then start/end offsets expressed in 4-byte words. This project reimplements only this small wire contract; no third-party source code is vendored.

The canary asks for offset 0, size 1024 words = 4096 bytes.

## Response parser

A `0x09` payload begins with the channel id. Before media data, header records are encoded as `BE16 length`, one-byte header id, and `length-1` data bytes. A zero length terminates the header phase. Header `0x03`, when exactly four data bytes long, is interpreted as a file size in 4-byte words for diagnostics only. Subsequent channel payload is counted and discarded. An empty packet in data state closes the channel.

The implementation does not allocate a media-sized buffer. It retains only counters and framing state. A successful reachability canary is declared once at least the requested 4096 data bytes have been observed, or when the channel closes after returning nonzero data.

## Safety and isolation

- One canary per selected track.
- Preferred metadata AudioFile candidate only.
- 4096 requested bytes, 5 second response timeout.
- Track changes cancel local pending state and late packets are counted as stale.
- No AES key, AudioFile body, expanded URL or reusable credential is exposed in `/json/info`.
- No decrypt, container parser, decoder or PCM feed in this gate.
- No `esp_http_client`, `esp_tls`, `WiFiClientSecure` or `NetworkClientSecure`.

## Hardware classification

Expected outcomes are deliberately non-ambiguous:

- `ok>=1`, `dataBytes>0`: AP encrypted-media transport reachable.
- `lastCmd=0xa`, `failures>=1`: Spotify explicitly rejects the AP media channel; record `failureCode`.
- `timeouts>=1`: no channel response before the bounded timeout.
- `protoErr>=1`: response framing differs from the historical contract; do not conflate this with a service rejection.

In every case verify `Shannon macFail=0` and the frozen audio regression separately.
