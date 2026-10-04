# dev.2 cspot integration plan

Pinned reference proven on Arduino ESP32 3.3.8 / ESP-IDF 5.5.4:
- cspot: 1b07a9c00ba6d5e878e5b33dcbf89bff493cde26
- bell: e83737367a08b5a5a1f652a7ecb97a0d926929dd
- ZeroConf service: `_spotify-connect._tcp`, port 8080
- Auth cache: `/spotify_auth.json`

The integration must keep the WLED main loop non-blocking. cspot networking/decoder tasks feed `WavesharePcmOutput::enqueue()`; the output task owns blocking `i2s_write()`. `volumeChanged(uint16_t)` maps to ES8311 hardware volume and cspot software volume is disabled.

First dev.2 gate: device visible in Spotify -> ZeroConf provisioning -> track load -> 44.1-kHz PCM -> speaker, while WLED HUB75 remains stable.
