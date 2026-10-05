# dev.2h-r1a Track metadata gate

## Qualified baseline

The baseline is dev.2g-r3, hardware-qualified through official Spotify Connect device activation: AP/Shannon authentication, persistent Mercury, SPIRC descendant URI dispatch, remote `Load`, local active `Notify`, and Mercury acknowledgement all pass without reconnect or Shannon MAC failure.

## New gate

On each remote `Load`, dev.2h-r1a identifies the selected SPIRC `TrackRef` from `State.track` (field 27), using `playing_track_index` when present and falling back to the first track. It records the 16-byte GID as 32 lowercase hex characters and the Spotify URI.

For a valid 16-byte GID it sends a Mercury `GET` to `hm://metadata/3/track/<gid-hex>`. The response header status code and body size are recorded. A status-200 body is decoded as the legacy `spotify.metadata.Track` schema used by librespot-compatible Mercury metadata clients: track name, album, artists, duration, album covers, and audio-file list. For future playback work the gate records a preferred OGG Vorbis 160 file (format enum 1) when present, otherwise the first available audio file. No audio key is requested in this build.

## Qualification criteria

A hardware PASS requires: a selected TrackRef with 32-hex GID, `Metadata GET attempts>=1`, a matching response with status 200, `ok>=1`, `parseFail=0`, non-empty title/artist/album and plausible duration. Shannon must remain `macFail=0`, reconnects must remain zero in the normal path, and the qualified audio backend must continue to report no I2S errors/short writes/late writes/ring underruns.

## Deliberately out of scope

Audio-key acquisition, CDN/storage resolution, media download, decryption, Vorbis/other codec decode, album-art network fetch/rendering, and feeding Spotify PCM into the output backend remain later gates.

## r1a compile correction

Arduino-ESP32 `Print.h` defines `HEX` as a macro. The r1 local `bytesToHex()` lookup identifier `HEX` therefore failed preprocessing on the target. r1a renames only that local lookup table to `kHexDigits`; protocol and runtime behavior are unchanged.
