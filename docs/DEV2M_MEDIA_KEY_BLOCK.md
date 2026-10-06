# dev.2m - media-key service-block hardening

## Why this gate exists

The receiver's RequestKey path is no longer an unqualified implementation guess. On real hardware, dev.2i-r2 correlated all four AudioFile candidates and Spotify returned `AesKeyError 0x0e / 0:1` for every format. A separate current librespot 0.8.0 native-Windows run, same account/network, independently returned `audio key 0 1` on multiple normal tracks. dev.2l-r2 meanwhile proved that encrypted AudioFile bytes remain reachable over AP StreamChunk.

The engineering problem is therefore to keep the Connect receiver stable while the account/service keymaster is blocked, not to hammer the same known-failing RequestKey on every track.

## r1 behavior

A fresh usermod session preserves the dev.2i bounded candidate scan. After the final candidate response, r1 inspects the complete result set. The session-local latch is activated only when every candidate has:

- command `0x0e`;
- error byte 0 = `0`;
- error byte 1 = `1`.

If any candidate times out, uses another error code or produces a protocol error, the latch is not activated. This keeps the optimization tied to the exact independently reproduced service failure.

Once latched:

1. SPIRC, keepalive, Mercury, TrackRef and metadata continue normally.
2. A later track still builds its bounded AudioFile candidate inventory.
3. No new RequestKey is sent for that track; `suppressedTracks` increments.
4. The preferred file remains available to the already-qualified AP StreamChunk canary so media transport health can still be observed.
5. Automatic AP reconnect inside the same running task does not clear the latch.
6. A fresh `startNow()` clears the active latch and permits one new scan, so a later Spotify-side recovery can be detected.

## Telemetry

```text
MediaKey state=<idle|requesting|diagnostic|ready|service-blocked> blocked=<yes|no> blockEvents=<n> suppressedTracks=<n> blockErr=<a>:<b>
```

No raw AES key is serialized. The block error bytes are the two already-public service response bytes only.

## Hardware qualification

First track on the currently affected account:

```text
AudioKey requests=<candidate count> responses=<candidate count> ok=0
MediaKey state=service-blocked blocked=yes blockEvents>=1 suppressedTracks=0 blockErr=0:1
```

Then change track without restarting the session:

```text
Metadata GET ... ok increases / track fields change
AudioKey requests=<unchanged>
MediaKey state=service-blocked ... suppressedTracks>=1
AP Stream ... can still complete its bounded canary
Shannon macFail=0
```

A reconnect test should likewise preserve the latch within the same task.

## Frozen areas

This gate does not modify the RequestKey wire contract, AP cryptography, AP StreamChunk wire contract, audio backend, decrypt or decoder.


## r2 SPIRC controller-state hardening (hardware result: incomplete)

Hardware r1 exposed a separate control-plane defect after the media-key latch: the Android controller emitted repeated empty `Load` frames (`tracks=0`, no GID), and r1 acknowledged every one as a fresh active-playing transfer. The Spotify UI then lost normal playback controls. r2 keeps RequestKey/AP Stream/audio unchanged and fixes only state reconciliation.

- Transfer Notify includes the selected track URI; when Spotify omits it, the 16-byte GID is encoded to the canonical 22-character Spotify base62 track id.
- The selected track index is included in legacy SPIRC State.
- Once `0x0e / 0:1` is latched, the receiver emits an active `Pause` Notify with track/context/position/duration.
- Empty Loads and duplicate Loads for the already-active GID are ignored and counted.
- A later real track change is still accepted; with the latch active, RequestKey stays suppressed, the paused state is emitted for the new track, and the qualified AP Stream canary still runs.


## r3 current-SPIRC queue mirroring

Hardware on r2 proved that a Mercury SEND acknowledgement was not sufficient: the Android controller still exposed no usable playback controls and a user-selected second song never reached the receiver (`SPIRC load=1`, `Metadata GET attempts=1`). Source review against the current SPIRC State schema found a concrete encoder mismatch: r2 put the selected track URI in obsolete State field 1 and emitted no repeated field 27 TrackRef queue.

r3 keeps the media-key latch policy unchanged and changes only outgoing SPIRC State construction/retention:

- incoming State field 27 TrackRef messages are preserved verbatim, bounded to 96 TrackRefs and 12 KiB of TrackRef payload;
- transfer Notify mirrors the preserved queue instead of synthesizing obsolete State field 1;
- blocked-state Pause Notify reuses the same queue after the asynchronous AudioKey failure;
- field 3/index and field 26/playing_track_index are kept coherent with the selected track;
- context, position, shuffle and repeat are preserved;
- obsolete State duration field 9 is not emitted;
- a single TrackRef fallback is generated only when a Load did not carry a queue;
- empty/duplicate Load filtering from r2 remains unchanged.

New telemetry:

```text
SPIRC state tracks=<n> trackBytes=<n> truncated=<n> fallbackRefs=<n>
```

Expected first-track gate for the observed 84-track playlist is `tracks=84`, `truncated=0`, `fallbackRefs=0`, followed by an acknowledged blocked Notify and a controller UI that remains usable. The decisive second-track gate is a new SPIRC Load/GID/metadata while `AudioKey requests` remains unchanged and `suppressedTracks>=1`.


## r3 hardware result: queue fixed, command plane still incomplete

Real hardware with r3 confirmed the field-27 correction. The Android Now Playing page remained visible after transferring to WLED, with `SPIRC state tracks=86`, `trackBytes=1548`, `truncated=0`, `fallbackRefs=0`; both transfer and blocked Pause Notify were acknowledged and Shannon remained clean. This upgrades the queue/state encoder to PASS.

The same run also exposed the next missing layer. Pressing Next increased remote SPIRC traffic (`rx=9 -> 19`, `load=1 -> 9`, `play=1 -> 2`) but the receiver remained on TrackRef index 5 / the same GID; metadata requests stayed at 1, `suppressedTracks` stayed 0 and AP Stream stayed at the original three probes. Eight Load frames were classified as duplicates. The phone could preview a different title but the receiver did not adopt it. Therefore r3 is only a partial controller interoperability pass: state presentation is coherent, command execution is not.

## r4 classic SPIRC command/ack gate

r4 changes only the Mercury/SPIRC control plane and preserves RequestKey, the media-key latch, AP/Shannon transport, AP StreamChunk and the frozen Waveshare audio backend.

- Frame `seq_nr` and repeated `recipient` are parsed. Commands explicitly addressed to another device are ignored.
- DeviceState advertises capability `kCommandAcks` (type 10).
- A targeted command is acknowledged in outgoing Notify State using `last_command_ident` field 20 and `last_command_msgid` field 21.
- Core classic command types are recognized: Play `0x15`, Pause `0x16`, PlayPause `0x17`, Seek `0x18`, Prev `0x19`, Next `0x1a`.
- Play/Pause/Seek produce a coherent control Notify. (This r4 policy deliberately forced Pause while the media key was blocked; r5 supersedes that policy after hardware showed it collapses the official controller UI.)
- Next/Prev move through the retained TrackRef queue, publish the new index/GID, request metadata for that TrackRef and then use the existing service-block suppression path: no new RequestKey, but the qualified AP Stream canary may run for the new file.
- Empty/duplicate Load retries are acknowledged with the retained coherent state; counters distinguish ignored vs acknowledged retries.

New telemetry includes:

```text
SPIRC rx=... play=... pause=... playPause=... seek=... prev=... next=...
SPIRC blocked Notify ... duplicateLoadIgnored=... duplicateLoadAcked=...
SPIRC control Notify sent=... ack=... bytes=... commandAcks=... recipientIgnored=...
```

The decisive hardware gate is one Next press after the first service-block latch. A PASS requires a receiver-side index/GID/metadata change, an acknowledged control Notify, unchanged `AudioKey requests`, `suppressedTracks>=1`, and another successful 3x4 KiB AP Stream canary for the new track.


## r4 hardware result: command ACK and Next qualified

Real hardware qualified the r4 command plane. One Next press produced `next=1`, control Notify `sent=5 ack=5`, `commandAcks=6`, `recipientIgnored=0`, and advanced the receiver from queue index 5 / Jabdah to index 6 / Blinding Lights. Metadata requests advanced from 1 to 2, `AudioKey requests` stayed at 4, `suppressedTracks=1`, and AP Stream advanced to `attempts=6 ok=6`. Thus queue navigation, command acknowledgement and media-key suppression are all independently working.

The same run exposed two remaining UI defects. Pressing Play stayed active only about three seconds because r4 intentionally translated Play back to Pause whenever `mediaKeyServiceBlocked_` was set. Direct track selection from the playlist was also not adopted, even though Next worked.

## r5 interactive Play/direct-selection gate

r5 keeps the media-key latch and all qualified transport unchanged, but removes the incorrect coupling between AES-key availability and SPIRC playback state. The receiver is allowed to report Playing while audio is intentionally silent. A bounded virtual position clock maintains a coherent position for later Pause/Seek/control Notify updates.

A Play frame is also inspected for `playing_track_index` or State index. If the controller uses Play to select a different item from the already-retained field-27 queue, r5 resolves that index locally, publishes the selected TrackRef, starts metadata for the new GID, suppresses the already-known failing RequestKey scan, and runs the existing AP Stream canary.

New telemetry:

```text
SPIRC ... playSelect=<n> byIndex=<n> ...
SPIRC playback status=<0|1|2> clock=<running|held> basePos=<ms>
```

Hardware PASS requires: (1) Play remains in state 1 for at least 10 seconds without an automatic Pause; and (2) direct playlist selection changes the receiver GID/metadata, with `AudioKey requests` unchanged and `suppressedTracks`/AP Stream advancing.


## r6 context-player result

Hardware r6 disproved the assumed historical JSON shape for the current Android client. The first real payload was `frames=1 bytes=709 endpoint=none`; direct list selection still did not advance the receiver. The payload is therefore treated as a protocol-version observation rather than a failed JSON string search.

## r7 current-protobuf / capability gate

Current public SPIRC defines `Frame.context_player_state` as bytes. Contemporary protocol definitions identify the current object as `spotify.player.esperanto.proto.ContextPlayerState`: field 6 is `ContextIndex`, field 7 is `ProvidedTrack`, and `ProvidedTrack.context_track` contains URI/UID. r7 parses only those bounded identity/index fields and never stores or emits the raw payload.

The same review exposed a capability mismatch: the usermod advertised classic `kSupportsPlaylistV2` even though it does not implement the full playlist-v2/connect-state command contract. r7 removes that capability advertisement while retaining the hardware-qualified classic field-27 queue, command ACKs and Play/Pause/Next handling. This both gives Android a truthful capability set and allows a current binary context-player Load/Replace to be resolved if one is still sent.

## r7 hardware result: direct row tap is still classic SPIRC

The 2026-10-06 r7 hardware A/B closes an important routing question. After reset and initial transfer, the current Android controller reported one classic Load and one field-19 payload (`contextPlayer frames=1`, total bytes 721). Tapping a different playlist row then produced a **second classic SPIRC Load** (`load=2`) and a second field-19 payload (`frames=2`, cumulative bytes 3644), while the top-level classic State still pointed at the original queue index/GID and metadata remained unchanged. The second Load was consequently classified as duplicate and ACKed (`duplicateLoadIgnored=1`, `duplicateLoadAcked=1`).

This means direct selection is not absent from the classic bus and does not justify a Dealer/TLS implementation yet. The selected identity is carried inside field 19 in a shape that did not match either the historical JSON `endpoint/skip_to` payload or the first current-protobuf identity hypothesis; r7 classified it as `binary`.

## r8 opaque field-19 discriminator

r8 keeps the known parsers as compatibility fallbacks but makes no new schema assumption. For the most recent field-19 payload it records only bounded, non-secret diagnostics:

```text
SPIRC contextDiag lastBytes=<n> hash=0x... prefix=<first-16-bytes-hex> magic=<none|json|gzip|zlib|zstd> printable=<pct>% proto=<yes|no> fields=<n> lenFields=<n> map=<field:wire,...>
SPIRC contextScan queueMatches=<n> nonCurrent=<n> uniqueIndex=<i|-1>
```

The raw field-19 body is frame-local, capped by the existing 12 KiB state bound, never persisted and never serialized to `/json/info`.

For functionality, r8 compares the opaque bytes against the exact URI and 16-byte GID identities already present in the retained classic field-27 TrackRef queue. It only changes the selected track when **exactly one non-current queue entry** is present. This is intentionally conservative: if the payload contains an entire context or multiple queue identities, the firmware records the counts and ACKs current state rather than guessing. A duplicate Load that reaches this path without a unique selection increments `duplicateCpsUnknown`.

This gate should tell the next step directly: a compression magic leads to bounded decompression; a valid protobuf wire map leads to exact schema reconstruction; a unique queue identity can qualify direct selection immediately.


### r8a compile-only correction

r8a changes no runtime or protocol behavior. It only renames the local hexadecimal lookup table in `contextPlayerPrefixHex()` from `HEX` to `HEX_DIGITS`, avoiding the Arduino `Print.h` macro `#define HEX 16` that prevented target compilation of r8.


### r9/r9a gzip context-player decoder

Real-hardware r8a diagnostics on 2026-10-06 resolved the opaque field-19 envelope. Current Android sends `Frame.context_player_state` as a gzip member: the diagnostic prefix is `1f8b08000000000000ff`. A settled current-track payload measured 2259 compressed bytes (`hash=0xf3cd6647`); tapping a different playlist row produced another classic SPIRC Load and a different 6332-byte gzip member (`hash=0x7fcabe10`). Raw queue scanning reported zero matches because the URI/GID identities are inside the compressed member.

r9 adds a single-member gzip reader with a strict 131072-byte decompressed ceiling. Hardware immediately showed a reset when the first Load reached this path. The cause is the high-level miniz `tinfl_decompress_mem_to_mem()` helper: it creates `tinfl_decompressor` as a large local object, consuming roughly 11 KiB on the already-active Spotify AP task stack. r9a keeps the same gzip contract but heap-allocates the decompressor state (internal 8-bit RAM preferred) and calls low-level ROM `tinfl_decompress()` directly. The trailer ISIZE still controls the bounded output allocation, PSRAM is preferred for the transient body, and gzip CRC32 is verified before the body is trusted. No decompressed body is persisted or exported.

After successful inflate, the existing bounded context-player parser is applied to the decompressed body. Explicit `skip_to` URI/index evidence is preferred; otherwise exact retained field-27 TrackRef URI/GID correlation is attempted. r9 refuses to adopt the current track as a selection and still refuses ambiguous queue matches. New diagnostics are `byInflate` and `SPIRC contextInflate attempts/ok/failures/status/bytes/printable/encoding`.

The r9a hardware PASS requires a direct row tap to produce `contextInflate ... status=ok`, a non-current selection (`select>=1`, normally `byInflate>=1`), new TrackRef/GID/metadata, unchanged AudioKey request count under the service-block latch, and another qualified AP Stream 3/3 probe.


### r10 structural JSON skip target

Hardware r9a qualified the stack-safe gzip decoder: current Android field-19 inflates with `status=ok`, CRC-valid output, 100% printable JSON, and `endpoint=play`. The first transfer produced 11803 decoded bytes; a direct row tap produced 20621 decoded bytes. The selector remained unresolved, proving the remaining failure is above gzip and inside the JSON target extraction.

r10 removes exact `"key":"value"` assumptions from the selector. JSON key lookup now tolerates whitespace around `:`, `skip_to` is bounded by its actual composite-object extent, and UID-only selection is resolved by locating the matching `uid` member and reading the `uri` sibling from the same enclosing track object. This matches the classic context-player play shape where `options.skip_to.track_uid` refers to an entry in `context.pages[].tracks[]`. No raw or decompressed JSON is persisted or exposed.


### r11 multi-skip target resolution

Hardware r10 did not resolve a direct playlist-row tap even though gzip decode remained stable and the decoded body was 100% printable JSON with `endpoint=play`. The first transfer produced `contextInflate bytes=1412` and `unresolved=1`; a direct row tap delivered a second classic Load and reported the last decoded body as `bytes=20634`, `unresolved=2`, while TrackRef/GID/metadata remained unchanged. Therefore the remaining failure is target resolution inside valid JSON, not transport, compression or SPIRC routing.

r11 enumerates up to 16 structurally valid `skip_to` objects in the decoded command instead of assuming the first one is authoritative. For each object it considers only explicit `track_uri`, `track_uid` (resolved to the sibling URI in the matching context track object) and bounded `track_index`. A new track is adopted only when all resolved non-current candidates converge on exactly one retained field-27 queue index. Ambiguous or current-only candidates are ACKed without guessing. New telemetry is `bySkip` plus `SPIRC contextSkip objects/uid/uri/index/uidResolved/queueResolved/uniqueIndex/ambiguous`. No target UID or raw/decompressed JSON is exposed.


### r12 canonical GID/URI queue identity

Hardware r11 on 2026-10-06 closed the modern-context half of the direct-row mapping: the gzip member inflated cleanly to JSON, one `skip_to` object exposed `track_uid`, and the UID resolved to its sibling Spotify track URI (`uidResolved=1`). The remaining failure was entirely at the classic queue boundary (`queueResolved=0`). In the same session `SPIRC state tracks=82 trackBytes=1476`; 1476 / 82 = exactly 18 bytes per retained TrackRef, which is the protobuf size of field 1 (length-delimited) carrying a 16-byte GID and no URI field.

r12 therefore preserves the r11 bounded multi-`skip_to` parser and changes only queue identity matching. Each retained TrackRef is canonicalized to a track URI: an existing field-2 `spotify:track:` URI is preferred; otherwise a valid 16-byte field-1 GID is converted with `spotifyTrackUriFromGid()`. The UID-resolved modern URI is compared against that canonical identity. Duplicate canonical matches remain a hard failure rather than a guess.

A second safety correction removes naked modern `track_index` as an authority. Context indices may be page-relative, so r12 records them only as validated when they agree with a URI/UID-resolved classic queue index; otherwise they are ignored. JSON selection always runs the full bounded multi-skip convergence before any single-target shortcut.

New non-sensitive telemetry is `SPIRC contextQueue refs/gidOnly/nativeUri/canonical` plus `contextSkip indexValidated/indexIgnored`. No GID, URI, UID or decompressed JSON is newly exposed.

Hardware PASS for r12: on the observed queue `contextQueue canonical=refs` with `gidOnly>0`; a direct playlist-row tap yields `uidResolved>=1`, `queueResolved>=1`, one non-current `uniqueIndex`, TrackRef/GID/metadata advance, AudioKey requests remain suppressed at the session latch, and AP Stream completes another 3/3 probe.


### r12 hardware qualification / r13 latency measurement / r14 poll optimization

Real hardware on 2026-10-07 qualified the r12 canonical mapping. The retained queue contained 84 TrackRefs / 1512 bytes, all reported as `gidOnly=84`, `nativeUri=0`, `canonical=84`. Two consecutive direct playlist taps resolved uniquely (`uniqueIndex=4`, then `uniqueIndex=2`) with `uidResolved=1`, `queueResolved=1`, `ambiguous=0`; TrackRef, metadata, suppressed-track count and AP Stream advanced exactly once per selection. The user observed a perceptible delay of about one second between tap and visible change.

r13 is intentionally measurement-only. It records local context-resolution time, successful Load-to-control-Notify apply time, and metadata Mercury round-trip latency. Hardware measured `resolveLast=38630/36821 us` and `applyLast=47148/45287 us` on two direct taps, while metadata RTT varied from 1503 ms to 255 ms. This proves the local resolver/apply path is only about 45-47 ms once the Load has arrived; the perceived ~1 s delay is therefore not caused by the r12 identity resolver. The AudioKey candidate line also labels later blocked tracks as `scan=suppressed`, avoiding confusion when current-track candidates have no RequestKey result while aggregate session counters still show the original 4/4 rejected scan.

r14 changes only the idle AP receive poll from 250 ms to 50 ms. It is a conservative scheduling optimization, not a semantic change: direct-selection resolution, SPIRC Notify, metadata, RequestKey suppression, AP StreamChunk and audio behavior remain frozen. The optimization can save at most 200 ms of local wait before noticing a newly available AP packet; perceptual latency reduction is optional, while stability is the qualification criterion.
