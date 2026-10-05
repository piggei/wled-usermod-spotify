# Next milestone after dev.2e-r2

Do not change the qualified shared-I2S/ES8311/DMA/PCM path, the dev.2d LoginBlob
credential path, or the r1c AP resolver/TCP transport unless a regression
requires it.

The current r2 gate ends immediately after encrypted AP authentication. Once
hardware testing shows repeatable `APWelcome` (`0xAC`), the next milestone is a
**minimal long-lived Spotify control session**:

1. keep the authenticated Shannon socket open;
2. handle AP ping/pong and disconnect/reconnect safely;
3. add the minimum Mercury request/response dispatcher required by Connect;
4. establish the minimum SPIRC/device-control state so the receiver remains a
   usable Spotify Connect target after selection;
5. expose metadata/state callbacks without touching the audio backend;
6. qualify reconnects and memory/stack behavior before enabling track decode.

Only after the control plane is stable should the media path be added:

```text
track resolution / audio key / CDN
          -> decoder
          -> 44.1 kHz signed 16-bit stereo
          -> WavesharePcmOutput::enqueuePcm44100()
          -> qualified shared-I2S / ES8311
```

Spotify volume should map to the already-qualified ES8311 volume path rather
than introduce an independent software-volume backend unless protocol behavior
requires otherwise.
