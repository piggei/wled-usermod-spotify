# dev.2n-r20 - Virtual EOS / automatic queue advance

## Purpose

r20 adds the missing natural end-of-track transition to the current diagnostic-only Spotify receiver. r19 proved encrypted AP transport, but with the live decrypt/decoder consumer deliberately closed there is no decoder EOS event yet. r20 therefore uses the validated track duration from metadata as a temporary **virtual EOS** source.

## Behavior

Virtual EOS is eligible only when all of the following are true:

- the usermod owns the active SPIRC state;
- playback status is playing and the virtual clock is running;
- parsed metadata supplied a non-zero duration for the **current metadata generation**;
- the current position has reached that duration;
- the same metadata generation has not already emitted EOS.

EOS is evaluated only when the AP socket has no queued packet. A simultaneous remote `LOAD`, `PLAY`, `NEXT`, `PAUSE` or other command therefore wins the race and is processed first.

For a normal queue successor, r20 creates retained state for `trackRefIndex + 1`, resets position to zero, sends a SPIRC control Notify with `source=auto-next-eos`, and enters the existing metadata/media path for that identity. Per-track AP transports are not special-cased: their already-qualified track-change cancellation/wipe logic observes the changed GID on the next AP-task iteration.

The decision is one-shot per metadata generation. Repeat and end-of-queue are conservatively held and separately counted rather than guessed.

## Safety / scope fence

r20 does **not** open live AES, Vorbis or PCM. It does not alter RequestKey serialization, r19 sustained transport, exact tail handling, ring storage, Shannon, metadata parsing, or audio output. The virtual clock is explicitly temporary; when a legitimate live decoder is available, decoder EOS should become the authoritative source and the virtual fallback can be disabled.

## Telemetry

Expected new lines:

```text
SPIRC EOS source=virtual-clock events=<n> generation=<g> metadataGeneration=<g> lastAction=<...>
SPIRC autoAdvance attempts=<n> ok=<n> boundary=<n> repeatHold=<n>
```

After a normal automatic transition the existing playback line should show:

```text
SPIRC playback status=1 clock=running basePos=<near zero> source=auto-next-eos ...
```

## Hardware gate

1. Flash r20 with the same `platformio_override.ini` used for r19.
2. Select a track with a known next queue item and do not issue remote commands near its end.
3. At natural end, verify the TrackRef index increments by exactly one, `events=1`, `attempts=1`, `ok=1`, `lastAction=advance`, and the new track starts near position zero with `source=auto-next-eos`.
4. Wait several seconds and verify the index does not skip again; the next EOS must belong to the new metadata generation.
5. Verify the new track independently starts the existing metadata/canary/Continuous/Extended pipeline and Shannon remains clean.

Optional guard: pause close to the end and wait past the nominal duration. No EOS should occur while the clock is held; after Resume it may occur when the position reaches duration.
