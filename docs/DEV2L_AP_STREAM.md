# dev.2l - bounded AP StreamChunk transport gate

## r1 hardware result

`v0.1.0-dev.2l-ap-stream-r1` reached the AP media channel on real Waveshare hardware. For the preferred format-1 AudioFile the receiver sent one 46-byte command `0x08` request for 4096 bytes and received command `0x09` channel traffic with three header records, a reported encrypted file size of 4,380,916 bytes and exactly 4096 media bytes. Shannon remained clean (`macFail=0`), the request had no timeout/protocol error, and the frozen I2S backend remained error-free.

The r1 `stale=1` is strongly consistent with a telemetry artifact: r1 declared success immediately after observing the requested 4096 data bytes, so a trailing channel-close packet would arrive after local `pending` had already been cleared and be counted as stale. r2 waits for explicit close so this interpretation is verified rather than assumed.

## r2 hardware result

`v0.1.0-dev.2l-ap-stream-r2` passed the sequential range gate on real Waveshare hardware. For `Close My Eyes Forever` (preferred format 1), telemetry reported `attempts=3 ok=3 failures=0 timeouts=0 protoErr=0 stale=0 postComplete=0 pending=no`, `probe=3/3`, `offset=8192`, `totalRequested=12288`, `dataBytes=12288`, three data packets and nine response packets. The reported encrypted file size was 5,564,912 bytes. Shannon remained clean (`macFail=0`) and the frozen I2S backend remained error-free.

This confirms that r1's `stale=1` was a local completion-order artifact: waiting for explicit channel close eliminates it without special-casing or dropping valid channel traffic.

## r2 purpose

Qualify repeated bounded range access without buffering media and remove the r1 close-packet ambiguity. r2 requests three consecutive 4096-byte ranges from the preferred AudioFile:

- probe 0: byte offset 0
- probe 1: byte offset 4096
- probe 2: byte offset 8192

Each probe uses a fresh AP channel id and is sent only after the preceding channel has explicitly closed. Total requested media is 12,288 bytes. Media bytes are counted and discarded immediately.

## Protocol contract

The small historical wire contract remains unchanged:

- request command `0x08`
- success/data command `0x09`
- channel error command `0x0a`
- 46-byte request: BE16 channel id, fixed fields, 20-byte AudioFile id, then start/end offsets in 4-byte words

A `0x09` payload begins with the channel id. Header records use `BE16 length`, one-byte header id, and `length-1` data bytes. Zero length terminates the header phase. Header `0x03` with four data bytes is interpreted only as file size in protocol words for diagnostics.

## r2 close semantics

r2 no longer declares a range successful merely because 4096 bytes have arrived. It waits for the explicit empty data-state channel packet, then requires at least 4096 bytes for that probe before incrementing `ok` and starting the next range. Packets for an already completed channel are classified as `postComplete`, not `stale`.

A fully successful run should therefore show approximately:

```text
AP Stream attempts=3 ok=3 failures=0 timeouts=0 protoErr=0 stale=0 ... pending=no
AP Stream ... requested=4096 totalRequested=12288 probe=3/3 offset=8192 ...
AP Stream ... dataBytes=12288 format=1
```

`responsePackets`, header counters and data-packet counters are aggregate across the three probes.

## Isolation

The gate still does not perform AES decryption, container parsing, decoding or PCM feed. It does not add TLS, Login5, client-token or CDN access. RequestKey and `audio/WavesharePcmOutput.*` remain unchanged. No AES key, AudioFile body or reusable credential is exposed in `/json/info`.
