# dev.2n-r17 - bounded continuous encrypted AP transport

## Purpose

r17 advances only the encrypted-media transport boundary. AudioKey/DRM remains
closed: current WLED r16 and an independent upstream librespot `dev` build both
received explicit `AesKeyError 0:1` for the same primary GID/file pairs. The
current public go-librespot master was additionally audited and its PlayPlay
plugin is still a stub (`IsSupported() == false`, no token, no deobfuscator), so
there is no public PlayPlay implementation to transplant into this usermod.

Evidence snapshots used for this decision: librespot `dev` commit
`e023adbbf017ae1fc10d01531dbe50c409786f2d`; go-librespot master commit
`4dbc099f46da3529adaa85feb97c4f49deb0b130`. These are research baselines, not
runtime dependencies of the WLED module.

No client identity, market, RequestKey bytes, candidate order, r16 diagnostic
probe, AES implementation, Vorbis decoder or PCM sink is changed by r17.

## Transport sequence

The qualified dev.2l/dev.2n 3x4096-byte canary remains first and byte-frozen.
After all three canary ranges pass and the retained `MediaChunkSource` verifies
its byte count/hash/read/rewind contract, r17 starts a second phase on the same
preferred AudioFile (format 1 when the observed metadata exposes it):

- 16 sequential StreamChunk requests;
- 4096 bytes requested per range;
- absolute byte offsets 0, 4096, ..., 61440;
- total target 65,536 encrypted bytes;
- each request keeps the qualified AP StreamChunk wire builder and shared Shannon
  channel/channel-id allocator.

The secondary receiver has independent counters and channel correlation. It does
not change the original canary counters or handler.

## Bounded ring

`decoder/SpotifyApContinuousRing.*` owns at most 64 KiB and prefers
`MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`, with bounded internal-RAM fallback if PSRAM
allocation is unavailable. It contains encrypted media bytes only.

Producer invariants:

- every push supplies the absolute media offset expected next;
- backward offsets are classified as duplicates;
- forward jumps are classified as gaps;
- unread bytes are never overwritten;
- capacity pressure reports `backpressure` instead of silently dropping data;
- producer completion is accepted only after the declared target byte count.

The diagnostic consumer drains the ring immediately in 512-byte reads, updates an
independent FNV-1a digest, and wipes consumed storage. This keeps producer and
consumer paths separate while avoiding live AES/Vorbis coupling. At completion:

- producer bytes must be exactly 65,536;
- consumer bytes must be exactly 65,536;
- producer and consumer hashes must match;
- ring must be valid and at EOF;
- gap/duplicate/producer errors must all be zero.

Backpressure is telemetry, not an expected success condition for the 64 KiB
experiment. Host tests deliberately exercise it and wrap-around behavior so the
ring contract is not limited to the expected hardware happy path.

## Cancellation and failure classification

A track change is detected independently of the frozen metadata-request block by
comparing the GID captured when the continuous phase starts with the current
selected GID. An incomplete transfer is cancelled and wiped. Session/task teardown
also resets the ring.

Independent telemetry classifies:

- AP write failure;
- response timeout;
- StreamChunk failure code;
- malformed header framing;
- stale or post-complete channel packets;
- short/long/empty range closure;
- ring producer/order rejection;
- diagnostic consumer failure;
- final hash/byte/EOF mismatch.

None of these conditions makes a rejected AudioKey look successful.

## `/json/info` interpretation

A clean hardware pass should converge to values equivalent to:

```text
AP Continuous state=complete target=65536 received=65536 consumed=65536 ranges=16/16 pending=no
AP Continuous transport attempts=16 ok=16 failures=0 timeouts=0 protoErr=0 stale=0 ... offset=61440 rangeBytes=4096 ... format=1
AP Continuous ring storage=psram cap=65536 highWater=<bounded> buffered=0 produced=65536 consumed=65536 backpressure=0 gap=0 duplicate=0 producerErr=0 valid=yes
AP Continuous integrity producerHash=0x... consumerHash=0x... match=yes eof=yes ... consumer=diagnostic decrypt=closed keyGate=blocked
AP Continuous lastError=none
```

`keyGate=blocked` is expected while the legacy key service rejects the account.
Even if a future legitimate key becomes available, r17 itself does not feed it to
this consumer.

## Hardware gate

1. Build/flash the normal Waveshare target with the same PlatformIO override used
   for r16.
2. Start the same Premium account and select a normal track on the WLED receiver.
3. Wait until the existing AP canary is `3/3` and r17 reaches `state=complete` or a
   classified failure.
4. Save `/json/info`.
5. Change to a second track and repeat once to exercise track-boundary reset and
   a fresh 64 KiB sequence.

Pass criteria: 16/16 ranges, 65,536 producer and consumer bytes, matching hashes,
EOF, zero protocol/order/backpressure errors, Shannon `macFail=0`, no reconnect or
control-path regression, and live decrypt/decoder still closed.

A target compile/postbuild remains mandatory; host tests do not claim ESP32
link/runtime qualification.
