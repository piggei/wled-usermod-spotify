# WLED Spotify Connect Usermod

## Current development build: v0.1.0-dev.2c-pcm-ingress-r1

This build preserves the hardware-qualified dev.2b-r2 shared-I2S/ES8311
runtime and adds the exact **44.1 kHz / signed 16-bit stereo PCM ingress** that
cspot will use. In shared AudioReactive mode it performs 2:1 conversion to the
qualified 22.05 kHz stream before the PSRAM ring; standalone mode remains
44.1 kHz passthrough. See `docs/DEV2C_PCM_INGRESS.md`.

Direct hardware-path regression test:

```text
/spotify-test?action=start&tone=1000
/spotify-test?action=stop
```

cspot-like 44.1-kHz PCM ingress test:

```text
/spotify-test?action=start-pcm&tone=1000
/spotify-test?action=stop
```

---

## v0.1.0-dev.2a-r1 — Spotify Connect discovery gate

This build retains the qualified dev.1-r9 audio backend and adds the first Spotify Connect network gate: `_spotify-connect._tcp` mDNS advertisement plus `GET /spotify_info`. It is intentionally **discovery-only**: `POST /spotify_info` / `addUser` is rejected without storing or logging Spotify credentials. See `docs/DEV2A_ZEROCONF_GATE.md`.


Target: **WLED 17.0.0-devV5, vid 2607201, ESP32-S3_Waveshare_HUB75** on Waveshare ESP32-S3 RGB Matrix.

This first test build deliberately validates the hardware/audio half before importing cspot. It configures **I2S1 TX master at 44.1 kHz / 16-bit stereo**, drives the onboard **ES8311** and PA, and exposes diagnostics in `/json/info`. This is the exact PCM format cspot will provide in dev.2.

## Important dev.1 limitation

**Disable AudioReactive and the audio-mode Buzzer usermod before enabling this module.** On WLED 17, AudioReactive may retain PinManager ownership of the Waveshare I2S pins after it is switched off. Revision r4 therefore treats ownership only as diagnostic evidence and blocks Spotify master-TX only if the shared LRCK is physically toggling at startup. This avoids a false block while still protecting an active I2S0 microphone clock domain.

## Install

1. Copy `usermods/spotify_connect` into the WLED tree under `usermods/`.
2. Merge the example environment from `platformio_override.example.ini` into your existing override, preserving your current Waveshare/iDotMatrix flags and pinned dependencies.
3. Build `pio run -e waveshare_spotify_dev1`.
4. Flash and open **Config -> Usermods**. Enable `Spotify Connect`, keep volume around 30–40 for the first run, save and reboot.
5. Open `/json/info` and inspect `Spotify state` and `Spotify audio`.
6. Trigger the 44.1-kHz PCM path with `http://WLED-IP/spotify-test?hz=1000&ms=1500`. Start at low speaker volume.

Expected healthy state:

`audio gate ready (cspot transport pending dev.2)`

The dev.1 gate is passed when WLED/HUB75 remains stable, ES8311 initializes, the 1 kHz test is clean from the speaker, and I2S1 produces no write errors/short writes during the PCM injection.

## Why cspot is not mixed into this first binary

The current public MuseRadio Spotify POC has now established a very useful compatibility point: its cspot build uses **Arduino ESP32 3.3.8 + ESP-IDF 5.5.4**, the same toolchain family as this WLED17 baseline. That removes the earlier concern that cspot required IDF 6. The next build can therefore vendor the tested cspot revision and add ZeroConf/auth/storage-resolve on top of this qualified 44.1-kHz sink.

Planned cspot pins/format do not change: PCM stereo 44.1 kHz / 16-bit -> ring buffer -> I2S1 -> ES8311; Spotify volume -> ES8311 hardware volume.

## dev.1 acceptance data to send back

Please send `/json/info` after boot with this module enabled, plus any serial lines containing `Spotify`, `I2S`, `ES8311`, reboot/panic, or watchdog messages. Also report whether the HUB75 display remains visually stable.

## dev.1-r2 build fix

This repack fixes WLED 17 / Arduino ESP32 Core 3.3.x preprocessor name collisions found during the first real compile gate:

- `I2C_SDA` -> `WAVESHARE_I2C_SDA`
- `I2C_SCL` -> `WAVESHARE_I2C_SCL`
- `VERSION` -> `USERMOD_VERSION`

No runtime/audio behavior was changed. The functional version remains `0.1.0-dev.1`; this archive is packaging/build revision `r2`.

## Packaging revision r3

`v0.1.0-dev.1-r3` combines the WLED 17 macro-collision fixes from r2 with the required
`build.libArchive=false` metadata in `./library.json`.


### dev.1-r4 note
WLED 17 may report the shared pins as owned by AudioReactive even while the AudioReactive UI state is off. r4 gates startup on physical LRCK activity instead of ownership alone. Keep AudioReactive disabled for the dev.1 hardware-gate test; runtime coexistence is not enabled yet.


## r5 packaging note
This package is flattened for use as an external WLED 17 usermod repository. `library.json`, `usermod_spotify_connect.*`, and `audio/` are at repository root.

## Package revision r6

The external-user-mod package now follows the same PlatformIO library layout used by the qualified WLED Buzzer usermod: `library.json` name matches the repository (`wled-usermod-spotify`), `build.srcDir` is `.`, `build.srcFilter` explicitly includes root and `audio/` C++ sources, and `build.libArchive` remains `false`. This is required for WLED 17 out-of-tree `custom_usermods` loading and for resolving the WLED include context (`wled.h`).


## r7 hardware-gate revision

The r7 packaging keeps the logical version at `0.1.0-dev.1` but changes the Waveshare clock policy to match the already hardware-qualified Buzzer shared-I2S topology. If AudioReactive owns the shared clock pins and LRCK is physically running, Spotify uses I2S1 TX **slave** mode and follows the I2S0 BCLK/LRCK clocks without reconfiguring the shared pads. If AudioReactive does not own the clocks, Spotify uses standalone I2S1 master at 44.1 kHz. If ownership exists but LRCK is absent, startup waits rather than briefly becoming a competing master.

For this bring-up revision the default Spotify volume is 70%. Existing persisted WLED configuration can still override it; `/json/info` reports the effective configured volume. Shared mode currently runs at the qualified AudioReactive-family rate of 22.05 kHz; this dev.1 test-tone path is for hardware qualification only. The future cspot transport must explicitly resample 44.1-kHz Spotify PCM when shared mode is active.


## r8 internal-heap hardening

The 64 KiB PCM stream buffer is now backed by PSRAM through `xStreamBufferCreateStatic()`. If PSRAM allocation fails, the module falls back to an 8 KiB internal buffer rather than consuming 64 KiB of internal DRAM. `/json/info` reports ring placement/capacity and internal heap before/after ring creation. This revision does not change the r7 shared-I2S clock topology.

### r9 note
The bring-up default volume is now 70%. WLED persists Usermod configuration, so an already-saved value (for example 40% from r8) remains authoritative until it is changed once in Config -> Usermods and saved.
