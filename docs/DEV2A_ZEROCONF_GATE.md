# dev.2a-r1 Spotify Connect Zeroconf discovery gate

## Purpose

This revision intentionally separates Spotify Connect discovery from authentication/session playback.
The qualified dev.1-r9 Waveshare audio backend is retained unchanged (including PSRAM PCM ring,
shared-I2S slave mode when AudioReactive owns the clocks, and 70% default volume).

## What r1 adds

- Advertises `_spotify-connect._tcp` on WLED's existing HTTP port 80.
- Adds mDNS TXT `CPath=/spotify_info` and `VERSION=1.0`.
- Handles `GET /spotify_info` with Spotify Connect device metadata and a development DH-public-key field.
- Adds `/json/info` counters for mDNS advertisement, getInfo requests and addUser attempts.
- Never logs, copies or persists `userName`, `blob` or `clientKey` during this discovery gate.
- `POST /spotify_info` is intentionally rejected until dev.2a-r2 adds real DH/LoginBlob authentication.

## Expected hardware test

1. Boot WLED with Spotify Connect enabled and Wi-Fi connected.
2. Confirm `/json/info` reports `mdns=advertised`.
3. Open Spotify on the same LAN and inspect **Connect to a device**.
4. Expected gate result: the configured Spotify device name appears.
5. Selecting it may fail in r1. That is expected; `addUser` must increment and `rejected` must match it.
6. Confirm audio test endpoint still works: `/spotify-test?hz=1000&ms=1500`.

## Pass criteria

- Device appears in the Spotify Connect device list.
- `getInfo` counter increases when Spotify discovers it.
- Selecting the device causes `addUser` to increase without reset/crash.
- Internal heap remains stable and audio test still has `err=0 short=0`.

## Next revision

`dev.2a-r2` will replace the development public key with a real DH keypair, decode Spotify LoginBlob
credentials in memory, persist only the resulting reusable credential blob as appropriate, and hand the
session to cspot/AP authentication. Full audio streaming remains a later dev.2b gate.
