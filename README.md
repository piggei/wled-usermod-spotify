# WLED Spotify Connect Usermod

Current development build: **v0.1.0-dev.2g-spirc-activation-r3**

Target baseline: WLED 17.0.0-devV5 / `ESP32-S3_Waveshare_HUB75` on the
Waveshare ESP32-S3 RGB Matrix board.

## Scope of dev.2g-r3

This revision starts from the hardware-qualified dev.2f persistent Spotify
session and adds the first SPIRC device-activation gate. The qualified audio
backend is unchanged.

Runtime path:

1. restore the cached Zeroconf/LoginBlob credential;
2. resolve/connect/authenticate to a Spotify AP;
3. keep the Shannon session alive and answer PING with PONG;
4. subscribe to `hm://remote/3/user/<username>/` through Mercury;
5. after the subscription is active, encode a minimal SPIRC `Hello` frame with
   the WLED device id, configured device name, volume and player capabilities;
6. send that frame as a Mercury `SEND` (`0xB2`) payload;
7. decode incoming SPIRC frames enough to classify Notify, Load, Play and Pause
   and expose activation diagnostics.

This is intentionally **not yet a Spotify audio-player build**. A received
`Load` is observed and counted, but this revision does not request track
metadata, audio keys, CDN data, decode media, or send Spotify PCM to the audio
backend. Those are separate later gates.

## Qualified baseline retained

Hardware-qualified blocks retained without algorithmic changes:

- dev.2d-r4 Zeroconf `getInfo` / `addUser` / LoginBlob decode;
- credential persistence and restore after reboot;
- repeated identical `addUser` credentials skip LittleFS rewrites;
- dev.2e-r1c AP resolve and bare TCP transport;
- dev.2e-r2 ClientHello/DH/Shannon/stored-credential authentication;
- dev.2f-r1 persistent Shannon session, PING/PONG and Mercury remote-user SUB;
- dev.2c 44.1 kHz signed 16-bit stereo PCM ingress;
- dev.2b shared-I2S/ES8311/DMA output and volume handling.

The dev.2f hardware run remained `session-active` for more than 170 seconds,
completed multiple keepalive cycles with `macFail=0`, and received a successful
Mercury subscription event. Four repeated Zeroconf `addUser` requests were
accepted without rewriting the cached credential.

## External-usermod layout

The source lives only in its external repository, for example:

```text
~/repo/wled-usermod-spotify/
```

WLED is only a build tree and does not need to be a Git repository. The intended
PlatformIO link is:

```text
wled-usermod-spotify = symlink://../wled-usermod-spotify
```

Do not keep a second in-tree copy under `WLED/usermods/`.

## Runtime diagnostics

With a cached credential, the session starts automatically after Wi-Fi is
ready. Manual controls remain:

```text
/spotify-session?action=stop
/spotify-session?action=reset
/spotify-session?action=probe
```

A healthy dev.2g gate should progress toward:

```text
state=spirc-ready
Shannon keys tx=32 rx=32 macFail=0
Mercury SUB attempts=1 ok>=1
SPIRC hello attempts=1 sent=1 ack>=1 bytes>0
SPIRC rx=... remote=... selfEcho=... notify=... load=... play=... pause=...
lastError=none
scope=SPIRC Hello/device advertisement + remote frame decode only; transfer/metadata/audio next gate
```

After selecting **WLED Matrix** in the official Spotify app, the most useful
new evidence is a non-zero SPIRC receive count and especially `load>=1`
(`MessageType 0x14`). Whether the app completes the visible device-transfer UI
is part of this hardware gate; it is not assumed by the source-only tests.

No username, auth blob, DH private key, shared secret, Shannon key or SPIRC
payload is printed in `/json/info`.

## Audio regression gate

The pre-existing qualification endpoint is unchanged:

```text
/spotify-test?action=start-pcm&tone=1000
/spotify-test?action=stop
```

After SPIRC testing the expected audio diagnostics remain `err=0`, `short=0`,
`late=0`, `ringUnderrun=0` with a clean continuous tone.

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

The malformed dev.2f postbuild rows for `FW_VERSION`, `FW_ZEROCONF` and
`FW_SESSION` have been corrected: every firmware check again has the explicit
`TARGET=firmware` column. `test_dev2g_spirc_contract.py` independently checks
SPIRC field numbers, the multipart Mercury `SEND` envelope, Hello markers and
the intentionally limited scope.

The full WLED/PlatformIO compile and hardware behavior remain qualification
steps on the target machine.

## Documentation

- `docs/DEV2D_LOGINBLOB.md` — LoginBlob credential gate.
- `docs/DEV2E_AP_TRANSPORT.md` — AP transport/authentication gate.
- `docs/DEV2F_MERCURY_SESSION.md` — persistent Shannon/Mercury gate.
- `docs/DEV2G_SPIRC_ACTIVATION.md` — this SPIRC Hello/device-activation gate.
- `THIRD_PARTY_NOTICES.md` — protocol/cryptographic provenance notes.


### dev.2g-r2 hardware finding

The r1 hardware gate proved that Spotify can deliver remote-user Mercury events on descendant URIs such as `hm://remote/3/user/<user>/<connection-id>`. r2 dispatches both the subscribed root URI and descendants to the SPIRC decoder. It also records the Shannon receive failure stage and declared payload size, and raises the bounded live encrypted-packet cap from 8192 to 16384 bytes for diagnosis without changing AP auth or audio behavior.


### dev.2g-r3 hardware target

Hardware r2 decoded descendant-URI SPIRC correctly (`rx=6`, `notify=4`, `load=2`) with zero reconnects. r3 adds the missing transfer acknowledgement: a remote `Load` makes the local SPIRC state active and emits `kMessageTypeNotify` with transferred context/position. Track acquisition and Spotify audio remain out of scope.
