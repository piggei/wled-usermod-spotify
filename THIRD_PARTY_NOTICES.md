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

## Media-key service-block hardening (dev.2m)

The dev.2m session latch is project-local control logic derived from hardware observations: it recognizes only the exact all-candidate `AesKeyError 0:1` condition independently reproduced by current librespot for the same account. It does not copy or vendor librespot/cspot implementation code and does not derive, expose or persist Spotify media keys.


## SPIRC State queue interoperability (dev.2m-r3)

dev.2m-r3 corrects the project-local SPIRC protobuf encoder to match the current wire schema observed/documented by contemporary Spotify Connect implementations: the playback queue is carried as repeated `State.track` / field 27 `TrackRef` messages. The implementation preserves bounded raw TrackRef submessages received from the controller and mirrors them in its own Notify state; no third-party implementation code is copied or vendored. Legacy State fields previously assumed for track URI/duration are no longer emitted.


## Classic SPIRC command acknowledgement interoperability (dev.2m-r4)

dev.2m-r4 independently implements protocol facts present in the public SPIRC protobuf schema and historical Connect receiver behavior: MessageType values for Play/Pause/PlayPause/Seek/Prev/Next, repeated `Frame.recipient`, capability `kCommandAcks`, and State fields `last_command_ident`/`last_command_msgid`. The project-local handler uses these wire facts to filter targeted commands, acknowledge them and update its retained queue. No librespot/cspot implementation source is copied or vendored.


## Current context-player protobuf / capability correction (dev.2m-r7)

dev.2m-r7 was cross-checked against the public current librespot protocol schemas.
`spirc.proto` declares `Frame.context_player_state` as bytes, while the current
Esperanto player schema defines a binary `ContextPlayerState` carrying `ContextIndex`
and `ProvidedTrack` / `ContextTrack` identity fields. The implementation below is a
small independent protobuf field reader using the project's existing wire helpers; no
librespot source is copied or linked.

The build also stops advertising `kSupportsPlaylistV2`. This is a capability-honesty
change: the project supports the qualified classic SPIRC queue/command path but not the
full modern playlist-v2/connect-state command contract.


## ESP32-S3 ROM miniz interface

The dev.2m-r9a diagnostic path uses the `miniz.h` low-level `tinfl_decompress` interface supplied by the Espressif ESP-IDF/ESP32-S3 ROM support already present in the target framework. The decompressor state is heap-allocated so the high-level helper does not consume the Spotify AP task stack. No miniz source code is vendored in this repository. Espressif's `esp_rom` miniz interface is distributed as part of ESP-IDF under its upstream licensing terms (header currently SPDX Apache-2.0).
