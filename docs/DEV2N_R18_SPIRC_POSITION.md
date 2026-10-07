# dev.2n-r18 - SPIRC track-change position reset

## Hardware observation

During the qualified r17 64 KiB transport test, direct selection changed the queue/media
identity correctly but the advertised SPIRC playback position did not restart from zero.
The first controlled track reported a running base position around 17 seconds and the
second new track inherited a base position around 18 seconds even though its GID, metadata
and encrypted media source had changed.

This did not affect AP byte offsets: r17 media transport uses independent absolute file
offsets and completed cleanly. It is nevertheless a real control-plane bug that would make
a future working player advertise/start the next track at the wrong timeline position.

## Cause

Current Android `context_player_state` frames can carry a running `positionMs` from the
previous state while also identifying a new `skip_to` target. The r17 direct-selection LOAD
path resolved the new queue index correctly but then reused that carried position when it
built the retained state for the new identity.

## r18 policy

A position is now classified by why the state is being sent:

- **new resolved track identity**: position is forced to `0` and telemetry source is
  `reset-track-change`;
- **initial Connect ownership transfer**: the controller-provided position is preserved
  (`remote-load`), because transfer can legitimately occur mid-track;
- **Pause/Resume/PlayPause on the current track**: the bounded virtual clock is preserved;
- **Seek**: the explicit requested position is used;
- **NEXT/PREV at a queue boundary with no actual index change**: the current position is
  preserved rather than rewinding the same track.

The same zero-on-change policy is applied to context-resolved LOAD, direct full LOAD after
local activation, REPLACE selection, PLAY-by-index selection and NEXT/PREV when the index
actually changes.

## Telemetry

`/json/info` extends the existing line:

```text
SPIRC playback status=<n> clock=<running|held> basePos=<ms> source=<reason> trackResets=<n>
```

`trackResets` increments only after a successful outbound state update whose media identity
changed and whose position was deliberately reset.

## Frozen areas

r18 does not change RequestKey, AudioKey probe, metadata audit, AP canary, r17 continuous
ring/transport, AES fixture, Vorbis decoder, PCM ingress, I2S/ES8311 or audio task code.
Live Spotify decrypt/decoder remains closed.

## Hardware gate

1. Flash r18 and transfer/select a normal track. Initial transfer may legitimately show a
   non-zero position with `source=remote-load` if ownership was transferred mid-track.
2. Direct-select a different track from the same queue.
3. Immediately inspect `/json/info`: the new `TrackRef`/metadata identity must be present,
   `SPIRC playback ... source=reset-track-change`, `trackResets>=1`, and `basePos` must be
   near zero (small elapsed time after the reset is expected).
4. Wait several seconds, Pause then Resume. The position must continue from the current
   track rather than resetting; source should reflect the current-track operation.
5. Perform a Seek. The advertised base position must follow the requested seek and source
   must become `seek-explicit`.
6. Confirm r17 transport still completes 16/16 with matching hashes and no gaps,
   duplicates, timeout, protocol error or backpressure.
