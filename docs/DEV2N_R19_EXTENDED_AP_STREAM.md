# dev.2n-r19 extended encrypted AP transport

## Scope

r19 extends only the encrypted-byte transport diagnostics. The qualified r17 3x4096
canary and 64 KiB continuous stage run first and remain the prerequisite. The r18 SPIRC
position semantics remain unchanged. AudioKey, decrypt, Vorbis and PCM live consumers
stay closed.

After r17 completes successfully, r19 performs two additional stages on the same selected
format-1 AudioFile:

1. **Sustained rolling transfer:** 256 sequential ranges x 4096 bytes = **1 MiB**,
   beginning at byte 65,536. Bytes are pushed through a separate PSRAM-preferred
   `SpotifyApContinuousRing` capped at 64 KiB and drained immediately by a hash-only
   diagnostic consumer.
2. **Exact EOF tail:** using the file size reported by AP header `0x03`, r19 computes the
   final 4096-aligned start and requests exactly the remaining bytes through the real file
   boundary. No request is intentionally made beyond EOF.

The sustained stage therefore exercises 256 AP request/response ranges while retaining a
64 KiB bound. With 1 MiB transferred, ring read/write positions complete 16 full address
cycles when the producer and consumer progress continuously.

## Integrity and bounds

The sustained producer and consumer maintain independent FNV-1a hashes and byte counts.
Completion requires:

- 256/256 full 4096-byte ranges;
- exactly 1,048,576 producer and consumer bytes;
- matching hashes;
- ring EOF after producer completion;
- zero backpressure, gap, duplicate and producer errors.

The exact-tail stage independently requires:

- AP-reported file size to remain consistent across responses;
- tail size between 1 and 4096 bytes and aligned to the protocol's four-byte word unit;
- producer/consumer byte counts equal to the computed tail length;
- matching tail hashes and ring EOF;
- `tailStart + tailBytes == reportedFileBytes`.

No returned media-key material is used or stored by this path.

## Track-change cancellation

If the selected GID changes while r19 is active, the in-flight ring is invalidated before
per-track state is cleared. Invalidation wipes buffered storage. Cumulative telemetry keeps:

- cancellation count;
- stage at cancellation (`sustained` or `tail`);
- bytes produced before cancellation;
- bytes still buffered at cancellation.

The next track must first pass its normal canary and r17 64 KiB stage before a fresh r19
sequence begins. Channel IDs are allocated from the existing AP StreamChunk namespace and
r19 recognizes only the contiguous channel span it allocated, so unrelated/late r17 packets
retain the prior handler's accounting.

## Telemetry

Expected `/json/info` lines include:

```text
AP Extended state=... stage=... fileBytes=... pending=...
AP Extended sustained start=65536 target=1048576 received=... consumed=... ranges=.../256 hashMatch=... eof=...
AP Extended transport attempts=... ok=... failures=... timeouts=... protoErr=... stale=... postComplete=... trackCancel=...
AP Extended sustainedRing storage=psram cap=65536 highWater=... produced=... consumed=... writeWraps=... readWraps=... backpressure=... gap=... duplicate=... producerErr=...
AP Extended sustainedIntegrity producerHash=0x... consumerHash=0x... reads=... consumer=diagnostic decrypt=closed
AP Extended tail start=... target=... received=... consumed=... producerHash=0x... consumerHash=0x... match=... eof=... exactBoundary=...
AP Extended cancel lastStage=... produced=... buffered=... wipe=yes
AP Extended lastError=...
```

## Hardware procedure

### Completion gate

1. Flash r19 with the same `platformio_override.ini` used for r18.
2. Select a track whose reported file is larger than 1,114,112 bytes.
3. Wait until `AP Extended state=complete`.
4. Capture `/json/info`.

A clean completion should show sustained 256/256, 1,048,576 received/consumed, matching
hashes, zero order/backpressure errors, and an exact tail with `exactBoundary=yes`.

### Cancellation gate

1. Start a fresh track and let r17 complete so r19 enters `stage=sustained`.
2. While `ranges` is still below 256, select a different track.
3. After the new track stabilizes, capture `/json/info`.

The cumulative `trackCancel` count must increase. `AP Extended cancel` must report the old
stage and `wipe=yes`. The new track must then be able to run its own canary/r17/r19 sequence
without accepting stale packets as current data.

## Non-goals

r19 does not attempt to recover, derive, deobfuscate, cache or use a Spotify media key. It
does not decrypt live AP bytes and does not feed live Vorbis or PCM. Successful r19 hardware
qualification therefore proves bounded encrypted transport lifecycle only, not Spotify audio
playback.
