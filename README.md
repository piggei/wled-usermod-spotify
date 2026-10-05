# WLED Spotify Connect Usermod

Current development build: **v0.1.0-dev.2h-track-metadata-r1a**

Target baseline: WLED 17.0.0-devV5 / `ESP32-S3_Waveshare_HUB75` on the Waveshare ESP32-S3 RGB Matrix board.

## Scope of dev.2h-r1a

Compile-only r1a correction: `Arduino.h`/`Print.h` defines the macro `HEX`; the local hexadecimal lookup table in `bytesToHex()` is therefore named `kHexDigits` to avoid a preprocessor collision. Runtime metadata/SPIRC/session behavior is unchanged from r1.


This build starts from the hardware-qualified **dev.2g-r3** Connect activation path and adds one isolated gate: selected-track metadata acquisition.

When Spotify sends a remote SPIRC `Load`, the usermod now:

1. keeps the qualified active `Notify` transfer acknowledgement;
2. extracts the selected `State.track` / `TrackRef` using `playing_track_index` when present;
3. records its 16-byte GID as 32 lowercase hex characters and its Spotify URI;
4. sends Mercury `GET hm://metadata/3/track/<gid-hex>`;
5. records the Mercury status/body size;
6. decodes the legacy `spotify.metadata.Track` body far enough to expose title, artists, album, duration, album-cover file id and audio-file inventory.

The metadata endpoint is intentionally a hardware gate: if Spotify has changed or disabled this legacy Mercury path, the telemetry will show the exact status/parse result before we add any more protocol layers.

Audio-key acquisition, storage/CDN resolution, media download, decryption, codec decode, album-art download/rendering and Spotify PCM playback remain out of scope. The qualified shared-I2S/PCM backend is unchanged.

## Frozen qualified baseline

The following blocks are retained without algorithmic changes:

- Zeroconf discovery, `getInfo`, `addUser` and LoginBlob decode;
- credential persistence/restore and identical-credential LittleFS write suppression;
- AP resolve + TCP;
- ClientHello, DH, Shannon and stored-credential AP authentication;
- persistent Shannon session, PING/PONG and Mercury remote-user subscription;
- descendant `hm://remote/3/user/<user>/...` SPIRC dispatch;
- SPIRC Hello;
- remote `Load` decode;
- local active SPIRC `Notify` transfer acknowledgement;
- 44.1 kHz signed 16-bit stereo PCM ingress;
- shared-I2S/ES8311/DMA output and volume handling.

The dev.2g-r3 hardware gate completed a real transfer from the official Spotify app: `load=1`, transfer `Notify sent=1 ack=1`, `localActive=yes`, `macFail=0`, reconnects `0`, and Spotify visibly connected to the Waveshare.

## Expected dev.2h diagnostics

After selecting **WLED Matrix** and transferring a track, `/json/info` should still show the qualified session/SPIRC path and additionally report lines similar to:

```text
TrackRef index=... gid=<32 hex chars> uri=spotify:track:...
Metadata GET attempts=1 responses=1 ok=1 parseFail=0 status=200 bytes=...
Track title=... artist=...
Track album=... duration=...ms covers=... coverId=...
Track audioFiles=... preferredFormat=... fileId=...
scope=SPIRC Load -> TrackRef -> Mercury track metadata; audio-key/CDN/decode next gate
```

For this first metadata gate the decisive evidence is:

```text
Shannon ... macFail=0
reconnect attempts=0
TrackRef gid=<32 hex chars>
Metadata GET attempts>=1
responses>=1
status=200
ok>=1
parseFail=0
```

Title, artist, album and duration should then be non-empty/plausible. Cover and audio-file inventories can legitimately vary by content, but when present their file IDs are exposed only as hexadecimal identifiers.

No username, auth blob, DH private key, shared secret, Shannon key or raw SPIRC/metadata payload is printed in `/json/info`.

## External-usermod layout

The source lives only in its external repository, for example:

```text
~/repo/wled-usermod-spotify/
```

WLED is only a build tree and does not need to be a Git repository. The intended PlatformIO link is:

```text
wled-usermod-spotify = symlink://../wled-usermod-spotify
```

Do not keep a second in-tree copy under `WLED/usermods/`.

## Manual session controls

With a cached credential the session starts automatically after Wi-Fi is ready. Manual controls remain:

```text
/spotify-session?action=stop
/spotify-session?action=reset
/spotify-session?action=probe
```

## Audio regression gate

The pre-existing qualification endpoint is unchanged:

```text
/spotify-test?action=start-pcm&tone=1000
/spotify-test?action=stop
```

After metadata testing the expected audio diagnostics remain `err=0`, `short=0`, `late=0`, `ringUnderrun=0` with a clean continuous tone.

## Tests

Project-specific checks remain data-driven:

```text
tests/release_checks.tsv
tests/hardware_checks.tsv
```

The generic runner remains:

```text
tools/test_runner.sh
```

Adding or changing a project test therefore does not require modifying the update script.

`test_dev2h_metadata_contract.py` guards the new TrackRef/Mercury metadata gate. The earlier AP, Shannon, LoginBlob, persistent-session and SPIRC contract tests remain active as regression guards.

The full WLED/PlatformIO compile and hardware behavior remain qualification steps on the target machine.

## Documentation

- `docs/DEV2D_LOGINBLOB.md` — LoginBlob credential gate.
- `docs/DEV2E_AP_TRANSPORT.md` — AP transport/authentication gate.
- `docs/DEV2F_MERCURY_SESSION.md` — persistent Shannon/Mercury gate.
- `docs/DEV2G_SPIRC_ACTIVATION.md` — qualified SPIRC activation/transfer gate.
- `docs/DEV2H_TRACK_METADATA.md` — current TrackRef/metadata gate.
- `THIRD_PARTY_NOTICES.md` — protocol/cryptographic provenance notes.
