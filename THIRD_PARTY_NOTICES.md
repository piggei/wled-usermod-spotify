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

This dev.2e gate therefore contains a small local wire encoder/decoder and an
independent Shannon primitive rather than importing the cspot runtime.
