# dev.2i-r2 Audio-key candidate diagnostics gate

## Qualified baseline

The baseline remains dev.2h-r1a. On target hardware it completed official-app Connect transfer, decoded TrackRef, fetched Mercury track metadata with status 200, parsed title/artist/album/duration and found multiple audio-file candidates. Shannon remained `macFail=0` and the shared-I2S backend remained error-free.

The dev.2i-r1 wire contract is also preserved: AP `RequestKey` (`0x0c`) body = 20-byte audio file id + 16-byte raw track GID + big-endian u32 sequence + big-endian u16 zero. A successful `AesKey` (`0x0d`) is sequence + 16 key bytes; `AesKeyError` is `0x0e` with the correlated sequence and service error bytes.

## r1 hardware result that motivated r2

The repeated 2026-10-06 hardware test reached the RequestKey gate correctly:

```text
Track title=Blinding Lights artist=The Weeknd
Track album=After Hours duration=200040ms
Track audioFiles=4 preferredFormat=1
AudioKey requests=1 responses=1 ok=0 errors=1 timeouts=0 pending=no
AudioKey seq=0 requestBytes=42 keyBytes=0 lastCmd=0xe err=0:1
Shannon macFail=0
```

Therefore r1 is no longer `PENDING/not triggered`: the request path was exercised and Spotify returned a real `AesKeyError 0x0e / 0:1`. It is not an audio-key PASS because no 16-byte key was returned.

## r2 diagnostic change

The metadata parser retains at most `MAX_AUDIO_KEY_CANDIDATES = 8` unique valid AudioFile ids. Candidate order is:

1. the same preferred file used by r1 when available (format `1` first under the current metadata policy);
2. the remaining valid unique AudioFile ids in metadata order.

Only one key request is pending at a time. The candidate index advances after either:

- a correlated `AesKeyError (0x0e)`; or
- the existing 2500 ms audio-key timeout.

The scan stops immediately after a valid `AesKey (0x0d)` or after all bounded candidates are exhausted. The RequestKey payload itself is unchanged.

## Track-change correlation hardening

A remote `Load` for a different 16-byte GID supersedes old metadata/key work. If an audio-key request is pending it is cancelled locally, the old sequence is no longer eligible for acceptance, and the new track starts with a fresh metadata/candidate set.

A later `0x0d/0x0e` response whose sequence is not the current pending sequence increments the `stale` counter and is ignored. It does not overwrite the current candidate result and is not accepted as the key for the new track.

Duplicate `Load` messages for a track that already has metadata/candidate work do not automatically start an unbounded second scan.

## Telemetry

The existing counters are retained and extended:

```text
AudioKey requests=<n> responses=<n> ok=<n> errors=<n> timeouts=<n> rejects=<n> protoErr=<n> stale=<n> trackCancel=<n> pending=<yes|no>
AudioKey seq=<n> requestBytes=42 keyBytes=<0|16> lastCmd=0x<d|e> err=<b0>:<b1>
AudioKey candidate=<current>/<count> format=<numeric> advances=<n> truncated=<n>
AudioKey candidates [0 f=<fmt> cmd=0x<...> ...] ...
```

`rejects` counts correlated Spotify `0x0e` responses. `protoErr` counts malformed/local protocol failures and should remain zero in a clean service-rejection test. `stale` records late sequence responses. `trackCancel` records in-flight key requests cancelled by a real track change.

The raw 16-byte AES key is never serialized. Candidate traces do not expose additional file ids.

## r2 qualification outcomes

### Candidate succeeds

```text
ok>=1
keyBytes=16
lastCmd=0xd
protoErr=0
pending=no
Shannon macFail=0
```

Earlier candidates may have `rejects>0`; this is still a useful PASS because a concrete file/format combination returned a valid key.

### All candidates are service-rejected

```text
requests == number of attempted candidates
responses >= number of correlated responses
ok=0
rejects == number of 0x0e results
keyBytes=0
protoErr=0
pending=no
```

If all candidate trace entries report `cmd=0xe err=0:1`, the evidence is much stronger that the failure is not limited to the originally preferred AudioFile.

### Timeout path

A timed-out candidate is marked `timeout` in the trace and the next candidate is tried once. Repeated automatic cycling is not allowed; the set is finite and bounded.

### Track-change path

Change track during the scan. The new GID must obtain its own metadata/candidate set; a late old key response may increment `stale` but must never produce `ok` for the new track.

## Deliberately out of scope

Storage/CDN resolve, HTTPS media access, download, media decryption, Ogg/Vorbis/AAC/other codec decode, PCM playback and album-art network fetch/rendering remain later gates. The qualified `WavesharePcmOutput` implementation is unchanged.

## Protocol provenance

The RequestKey wire contract follows the same public current librespot-style contract already documented for r1. No librespot source is incorporated. r2 changes only candidate selection/correlation diagnostics around that unchanged request.

## r2 hardware qualification result — 2026-10-06

The bounded scan was exercised on real hardware with `Beat Of Your Heart - Club Dub Edit`:

```text
Track audioFiles=4 preferredFormat=1
AudioKey requests=4 responses=4 ok=0 errors=4 timeouts=0 rejects=4 protoErr=0 stale=0 trackCancel=0 pending=no
AudioKey candidate=4/4 format=8 advances=3 truncated=0
AudioKey candidates [0 f=1 cmd=0xe err=0:1] [1 f=2 cmd=0xe err=0:1] [2 f=0 cmd=0xe err=0:1] [3 f=8 cmd=0xe err=0:1]
Shannon macFail=0
```

This qualifies the r2 candidate inventory and bounded-scan behavior. The rejection is not specific to the originally preferred AudioFile/format.

## Independent current-librespot A/B result

The same Spotify account and network were tested with a separately built current librespot 0.8.0 (`e023adb`) running natively on Windows. It successfully connected to `ap-gew4.spotify.com:4070`, authenticated, received `Country: "NG"`, loaded `Beat Of Your Heart - Club Dub Edit` and then independently reported:

```text
ERROR librespot_core::audio_key] error audio key 0 1
```

It repeated the same failure on `Beat Of Your Heart - Club Dub`.

This external reproduction strongly separates the observed `0:1` failure from the ESP32 RequestKey encoder, candidate selection, Shannon implementation and WLED audio backend. The valid-key success gate remains open because no `0x0d`/16-byte key has been obtained, but dev.2i-r2 is treated as technically validated and should not be rewritten without contrary evidence.

The next build is therefore dev.2j, a bounded ProductInfo/head-file reachability canary that does not require the blocked AES key. Full storage/CDN/decrypt remains separately gated.
