# dev.2n-r19 verification

## Intent

r19 qualifies a longer encrypted transport lifecycle above the hardware-qualified r17 and
r18 baselines. It adds a 1 MiB rolling AP transfer and exact EOF-tail request while preserving
the live decrypt/decoder fence.

## Host verification

The r19 native ring test compiles and executes the actual `SpotifyApContinuousRing` and
covers:

- 1 MiB delivered as 256 exact 4096-byte ranges;
- immediate 512-byte consumer drains;
- 16 write-position and 16 read-position ring wraps with a 64 KiB capacity;
- producer/consumer hash equality;
- a 2292-byte exact EOF-tail shape derived from an observed 4,380,916-byte media file;
- cancellation/invalidation with unread bytes discarded.

The r19 static/freeze contract verifies:

- r17's 3x4096 canary and 64 KiB stage remain explicit prerequisites;
- sustained geometry is 1 MiB/256 ranges beginning after the first 64 KiB;
- EOF-tail geometry derives only from AP-reported file size;
- channel ownership is isolated from the frozen r17 handler;
- track-change cancellation invalidates the ring before per-track state is cleared;
- no AES decryptor, Vorbis player or PCM enqueue call is connected to either r19 stage;
- audio sink, media source, AES fixture/decryptor, Vorbis fixture/player, metadata audit and
  manual AudioKey-probe implementation files remain frozen. The only decoder file changed by
  r19 is `SpotifyApContinuousRing`, and that change is additive wrap telemetry.

The retained r18 contract continues to verify new-track position reset, current-track
Pause/Resume and Seek semantics. The retained r17 contract continues to verify the qualified
64 KiB encrypted path and frozen r16 AudioKey/metadata baseline.


## Preparation result

The complete prebuild manifest passed with **212 PASS, 0 required failures and 0 optional
failures**. The r19 ring native test passed with both GCC and Clang. GCC ASan/UBSan also
passed for r19, and the retained r17 ring, r16 adapter/key-probe and r15 metadata parser
were re-run under sanitizers without failure.

No complete WLED/ESP32 target build was run in the preparation environment; target compile,
postbuild firmware checks and the two r19 hardware gates remain pending.

## Target-build limit

The preparation environment does not contain the complete WLED target tree/toolchain, so
`platformio run -e waveshare_spotify` and postbuild firmware-string checks remain required on
the user's build machine. Hardware completion and in-flight cancellation are separate gates.

## Expected hardware evidence

Completion requires `AP Extended state=complete`, sustained `ranges=256/256`,
`received=consumed=1048576`, matching sustained hashes, zero timeout/protocol/order/
backpressure errors, and a tail with matching hashes, EOF and `exactBoundary=yes`.

Cancellation qualification requires an actual track change while the sustained stage is
in-flight, an incremented `trackCancel` counter, recorded prior stage/byte counts and a clean
subsequent transfer on the new track.
