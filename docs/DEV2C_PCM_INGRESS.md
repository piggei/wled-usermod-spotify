# dev.2c PCM ingress gate

`v0.1.0-dev.2c-pcm-ingress-r1` freezes the hardware-qualified dev.2b-r2
ES8311/I2S path and validates the exact PCM contract that cspot will use next.

cspot supplies signed 16-bit stereo PCM at 44.1 kHz. The Waveshare shared-I2S
path is clocked by AudioReactive at 22.05 kHz with 32-bit I2S slots, so the
Spotify sink must convert the source rate before the existing PSRAM ring.

This build adds `WavesharePcmOutput::enqueuePcm44100()`:

- standalone I2S1 master: 44.1 kHz PCM passes through unchanged;
- shared I2S1 slave: 44.1 kHz PCM is low-pass averaged in pairs and decimated
  2:1 to 22.05 kHz before entering the ring;
- the already-qualified output task still expands signed 16-bit samples into
  the MSBs of 32-bit shared-I2S slots;
- I2S/ES8311 ownership, DMA 8x256, task priority/affinity and cold-boot volume
  behavior are intentionally unchanged from dev.2b-r2.

A development PCM producer mimics cspot at 44.1 kHz and feeds only this new
sink API. It is separate from the direct output-task tone used to qualify the
hardware path.

## Hardware gate

Direct regression path:

```text
/spotify-test?action=start&tone=1000
/spotify-test?action=stop
```

cspot-like PCM ingress path:

```text
/spotify-test?action=start-pcm&tone=1000
/spotify-test?action=stop
```

The PCM ingress gate passes when the `start-pcm` tone is clean, continuous and
at the correct pitch, and `/json/info` reports:

- shared output 22050 Hz / 32-bit;
- `PCM ingress 44100->22050`;
- input frames approximately twice output frames;
- `failures=0`, `feedFail=0`, `ringUnderrun=0`;
- I2S `err=0`, `short=0`, `late=0`.

This build intentionally does not yet import cspot authentication/session code.
Once this gate passes, the cspot AudioSink callback can feed
`enqueuePcm44100()` without changing the qualified ES8311/I2S backend.
