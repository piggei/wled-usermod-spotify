# Next: cspot session integration

The hardware audio backend is qualified on dev.2b-r2. dev.2c-r1 adds and
qualifies the cspot-facing PCM sink contract (`enqueuePcm44100`) independently
of authentication/session complexity.

Pinned reference proven on Arduino ESP32 3.3.8 / ESP-IDF 5.5.4:

- cspot: `1b07a9c00ba6d5e878e5b33dcbf89bff493cde26`
- bell: `e83737367a08b5a5a1f652a7ecb97a0d926929dd`
- ZeroConf service: `_spotify-connect._tcp`, port 8080
- Auth cache: `/spotify_auth.json`

After the dev.2c PCM gate passes, the next build should add the cspot
LoginBlob/session layer and an AudioSink adapter whose PCM callback does only:

```text
cspot 44.1-kHz / 16-bit stereo PCM
        -> WavesharePcmOutput::enqueuePcm44100()
        -> PSRAM ring
        -> qualified I2S1/ES8311 output task
```

The WLED main loop must remain non-blocking. cspot networking/session work must
run outside the WLED loop. Spotify volume should map to ES8311 hardware volume;
software PCM attenuation should stay disabled.
