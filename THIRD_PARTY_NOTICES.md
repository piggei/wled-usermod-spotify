# Third-party notices

The ES8311 playback initialization in this package is adapted from the Waveshare
ESP32-S3 RGB Matrix example code and from the Apache-2.0 ES8311 path previously
used in the WLED Buzzer Usermod. Preserve the applicable Apache-2.0 notice when
redistributing derived code.

## cspot / bell reference

`v0.1.0-dev.2c-pcm-ingress-r1` does **not** bundle cspot or bell source code.
Its 44.1 kHz PCM ingress contract is designed for the public cspot AudioSink
interface and is informed by the public cspot project and the MuseRadio ESP32-S3
integration reference.

The pinned revisions planned for the next integration step are:

- cspot `1b07a9c00ba6d5e878e5b33dcbf89bff493cde26`
- bell `e83737367a08b5a5a1f652a7ecb97a0d926929dd`

cspot and bell are MIT-licensed. When their source is actually vendored or
linked into a later package, include their applicable MIT license and copyright
notices in that distribution.
