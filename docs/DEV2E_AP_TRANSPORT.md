# dev.2e — Spotify AP transport and authenticated-session gates

## Qualified r1c baseline

The first dev.2e hardware gate established the network path independently of
the Spotify session protocol:

1. restore the qualified dev.2d credential;
2. query `http://apresolve.spotify.com/?type=accesspoint`;
3. accept the current `accesspoint` response key and the legacy `ap_list` key;
4. expose `ap.spotify.com:443` only as an explicit fallback;
5. parse the returned `host:port`;
6. open and close a bare TCP socket in a dedicated unpinned FreeRTOS task.

Hardware qualification on r1c returned a real `ap-...spotify.com:4070`
endpoint, HTTP 200, `fallback=0`, and repeated TCP successes. The shared audio
backend remained at zero I2S errors/short writes/late writes/ring underruns.

The AP connection is not HTTPS. Spotify performs its own key exchange over the
raw TCP transport and then protects AP packets with Shannon.

## r2 scope — authenticated AP session only

r2 retains r1c unchanged and extends the same task through:

```text
cached credential
      |
AP resolve + bare TCP
      |
ClientHello
      |
APResponseMessage / 96-byte DH public key
      |
DH shared secret + challenge HMAC
      |
ClientResponsePlaintext
      |
32-byte Shannon TX/RX keys
      |
command 0xAB + ClientResponseEncrypted
      |
0xAC APWelcome  -> PASS
0xAD LoginFailed -> distinct auth decline
```

The socket is deliberately closed after this gate. There is no long-lived
Mercury/SPIRC dispatcher and no Spotify audio decode in r2.

## Wire implementation

To keep this gate small, the handful of required protobuf fields are encoded
and decoded directly instead of importing a complete protobuf/cspot runtime.
The session uses the protocol field numbers required for:

- `ClientHello`, `APResponseMessage` and `ClientResponsePlaintext`;
- `LoginCredentials`, `SystemInfo` and `ClientResponseEncrypted`.

The handshake transcript contains the exact framed ClientHello bytes sent and
the exact framed AP response bytes received. Five HMAC-SHA1 blocks are derived
from the DH shared secret; the first block is used for the challenge response,
and bytes 20..51 / 52..83 become the 32-byte Shannon send/receive keys.

Shannon AP packets use:

```text
[command:1][payload length BE:2][payload:N][MAC:4]
```

with independent big-endian 32-bit nonce counters starting at zero.

## Security / diagnostic constraints

Runtime diagnostics report only states, byte counts, counters and command IDs.
They do not print:

- username;
- reusable authentication data;
- DH private/shared values;
- challenge material;
- Shannon keys;
- encrypted/decrypted auth payloads.

The reusable credential is copied only into the network task snapshot needed by
the probe and that byte vector is overwritten before the task exits.

## `/json/info` success gate

Expected success evidence:

```text
state=authenticated | credential=ready endpoint=...
AP handshake attempts=1 ok=1 clientHello>0 apHello>0 dh=96 challenge>0
Shannon keys tx=32 rx=32 macFail=0
AP auth attempts=1 ok=1 declined=0 request>0 response>0 lastCmd=0xac
lastError=none
```

`lastCmd=0xad` with `declined=1` is a valid encrypted-session exchange but a
failed stored-credential authorization and must not be counted as success.

## Hardware checklist

The authoritative manual gates are in `tests/hardware_checks.tsv`. In
particular r2 requires:

- `SESSION_HELLO`;
- `SESSION_SHANNON`;
- `SESSION_AUTH`;
- `SESSION_RETRY`;
- `AUDIO_AFTER_AUTH`.

## Next gate

Only after repeatable `APWelcome` should the connection be kept alive long
enough to implement the minimum Mercury/SPIRC control plane. Playback is still
separate: decoded 44.1 kHz signed 16-bit stereo must ultimately enter the
already-qualified `enqueuePcm44100()` path rather than creating a new audio
backend.
