# dev.2g-r3 SPIRC activation gate

## Goal

The dev.2f hardware gate proved that the ESP32-S3 can keep an authenticated
Spotify AP/Shannon session alive, answer keepalives and maintain the remote-user
Mercury subscription. The Spotify app still remained in its connection phase
because the receiver had not yet announced a SPIRC player state.

This revision adds only that missing protocol layer. It does not add media
resolution or playback.

## Startup sequence

After APWelcome and the qualified dev.2f startup:

```text
PING -> PONG
   -> Mercury SUB hm://remote/3/user/<username>/
   -> SUB becomes active
   -> build SPIRC Hello
   -> Mercury SEND (0xB2) with one SPIRC payload part
   -> receive SEND response / subscription frames
```

The SPIRC Hello contains the public `Frame` fields used by interoperable Spotify
Connect receivers: version, device ident, protocol version `2.7.1`, sequence
number, message type, device state, playback state and state-update timestamp.
The device state advertises the configured WLED device name, current usermod
volume, `can_play=true`, inactive initial state, and the basic player capability
set.

The protobuf wire format is encoded directly by the usermod. No generated cspot
sources or protobuf runtime are incorporated.

## Incoming-frame diagnostics

For Mercury subscription payloads the gate decodes only enough SPIRC data to
observe control flow. `/json/info` exposes counters for:

- total valid SPIRC frames;
- frames from another device versus own echoed Hello;
- Notify (`0x0A`);
- Load (`0x14`);
- Play (`0x15`);
- Pause (`0x16`);
- last message type and remote active flag;
- remote ident/name only as ordinary device diagnostics.

A `Load` proves that Spotify has progressed far enough to request a transfer to
this receiver. The revision deliberately does not act on the track payload.

## Deliberate exclusions

No track metadata lookup, audio-key request, CDN URL lookup/download, codec
integration, Spotify PCM production, queue management, seek or transfer-state
application is added here. The qualified PCM/I2S backend is unchanged.

## Hardware qualification

After flashing:

1. wait for the automatic session;
2. inspect `/json/info` for `SPIRC hello attempts=1 sent=1` and `macFail=0`;
3. select **WLED Matrix** in Spotify;
4. inspect whether `ack`, `rx`, `remote`, `selfEcho`, `notify` and especially
   `load` change;
5. note whether the Spotify UI leaves “connecting”;
6. run the existing 44.1 kHz PCM test as an audio regression check.

A visible UI transfer is desirable but the diagnostic gate is intentionally
more precise: it distinguishes Hello transport, acknowledgement, frame decode
and Load reception so the next failure domain is clear.


## r2 correction after hardware r1

Hardware r1 showed `Mercury lastUri=hm://remote/3/user/<user>/<connection-id>` while `SPIRC rx=0`. The r1 dispatcher required exact equality with the subscription root, so valid descendant events were not decoded. r2 accepts the root and descendants, adds root/child counters, and adds Shannon receive-stage/declared-length diagnostics.


## r3 correction after hardware r2

Hardware r2 proved stable descendant-URI SPIRC dispatch with no reconnects and decoded remote frames (`Notify` and `Load`) from the Android controller. The remaining Connect handoff gap is control-plane acknowledgement: after a remote `Load`, cspot marks the local device active, updates the transferred position/context, and immediately sends `kMessageTypeNotify`. r3 implements only that acknowledgement contract. It does not fetch metadata, audio keys, CDN data, or decode/play Spotify audio.
