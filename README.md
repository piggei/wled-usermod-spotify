# WLED Spotify Connect Usermod

Current development build: **v0.1.0-dev.2f-mercury-session-r1**

Target baseline: WLED 17.0.0-devV5 / `ESP32-S3_Waveshare_HUB75` on the
Waveshare ESP32-S3 RGB Matrix board.

## What this build does

This revision extends the hardware-qualified Spotify Connect path through the
first real Spotify access-point authentication gate:

1. restore the reusable credential previously obtained through Zeroconf/LoginBlob;
2. resolve a Spotify AP with `http://apresolve.spotify.com/?type=accesspoint`;
3. open the already-qualified bare TCP transport;
4. send `ClientHello` and read `APResponseMessage`;
5. solve the 96-byte Diffie-Hellman challenge and derive the AP keys;
6. send `ClientResponsePlaintext`;
7. initialize independent Shannon send/receive channels;
8. send the stored credential using Spotify command `0xAB`;
9. accept `APWelcome` (`0xAC`) as the qualification result, or report
   `APLoginFailed` (`0xAD`) distinctly;
10. close the socket immediately after the authentication gate.

**Mercury, SPIRC, track metadata, audio-key retrieval, media download, decoder
and Spotify playback are intentionally not enabled yet.** This keeps the
session/authentication failure domain separate from the already-qualified audio
runtime.

## Qualified baseline retained unchanged

The following blocks are treated as frozen unless a regression demonstrates a
need to change them:

- dev.2d-r4 Zeroconf `getInfo` / `addUser` / LoginBlob decode;
- reusable credential persistence in LittleFS and restore after reboot;
- target-compatible streaming SHA1/manual HMAC-SHA1 and software AES-192 used
  by LoginBlob;
- dev.2c 44.1 kHz signed 16-bit stereo PCM ingress and 44.1 -> 22.05 kHz gate;
- dev.2b shared-I2S/ES8311/DMA output, volume startup handling and PSRAM ring;
- generic TSV-driven prebuild/postbuild release checks.

The dev.2e-r1c AP resolver and TCP transport were also qualified on hardware
before this revision: the resolver returned a real `:4070` Spotify AP without
fallback and repeated TCP connections succeeded without disturbing the audio
backend.

## External-user-mod layout

The module lives in its own repository, for example:

```text
~/repo/wled-usermod-spotify/
```

WLED is only the build tree. The intended PlatformIO link is:

```text
wled-usermod-spotify = symlink://../wled-usermod-spotify
```

Do not copy another Spotify usermod into `WLED/usermods/` at the same time.
The update workflow does not require WLED to be a Git repository.

## Runtime gate

With a cached dev.2d credential already present, the AP-auth probe starts
automatically after Wi-Fi is ready. It can be retriggered when no probe task is
active:

```text
/spotify-session?action=reset
/spotify-session?action=probe
```

The diagnostic endpoint never returns the username, auth blob, DH private key,
shared secret or Shannon keys. `/json/info` reports only state, counters and
byte lengths.

A successful r2 qualification should look conceptually like:

```text
Spotify session:
state=authenticated | credential=ready endpoint=ap-....spotify.com:4070
AP resolve mode=http attempts=1 ok=1 http=200 bytes=... fallback=0
AP TCP attempts=1 ok=1 duration=...ms lastError=none
AP handshake attempts=1 ok=1 clientHello=... apHello=... dh=96 challenge=...
Shannon keys tx=32 rx=32 macFail=0
AP auth attempts=1 ok=1 declined=0 request=... response=... lastCmd=0xac
AP task attempts=1 heap=...->... minHeap=... stackMin=...
scope=ClientHello + DH + Shannon + stored-credential AP auth only; Mercury/playback next gate
```

If the server returns `0xAD`, the telemetry must show `state=auth-declined` and
increment `declined`; that is a protocol-level authentication result, not a TCP
failure.

## Audio regression gate

The pre-existing PCM qualification path remains available:

```text
/spotify-test?action=start-pcm&tone=1000
/spotify-test?action=stop
```

After session testing, the expected audio diagnostics remain `err=0`,
`short=0`, `late=0`, `ringUnderrun=0` with a clean continuous tone.

## Tests

Project-specific checks are data, not shell-script logic:

```text
tests/release_checks.tsv
tests/hardware_checks.tsv
```

The generic runner is:

```text
tools/test_runner.sh
```

Host-side checks include the LoginBlob/AES-192 regressions, AP wire-contract
checks and a Shannon known-answer encryption/MAC vector. The full WLED target
compile still has to run in the actual WLED/PlatformIO build tree.

## Documentation

- `docs/DEV2D_LOGINBLOB.md` — qualified Zeroconf/LoginBlob credential gate.
- `docs/DEV2E_AP_TRANSPORT.md` — qualified r1c AP resolve/TCP gate plus the r2
  authenticated-session extension.
- `docs/NEXT_DEV2_CSPOT.md` — next work after `APWelcome` is qualified.
- `THIRD_PARTY_NOTICES.md` — protocol/cryptographic references and licensing
  notes.


## dev.2f-r1

Adds persistent authenticated Shannon session, PING/PONG handling, country capture and the first minimal Mercury remote-user subscription gate. See `docs/DEV2F_MERCURY_SESSION.md`.
