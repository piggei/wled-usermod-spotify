# Third-party notices

The ES8311 playback initialization in this package is adapted from the Waveshare
ESP32-S3 RGB Matrix example code and from the Apache-2.0 ES8311 path previously
used in the WLED Buzzer Usermod. Preserve the applicable Apache-2.0 notice when
redistributing derived code.

## cspot protocol reference

`v0.1.0-dev.2d-loginblob-r1` does **not** bundle or link cspot/bell source code.
The Zeroconf/LoginBlob interoperability work was implemented in this usermod against
the publicly documented Spotify Connect/cspot protocol flow and is intentionally kept
separate from any future cspot runtime integration.

Before any later release vendors or links cspot/bell, review the exact upstream
revisions and include the licenses/notices required by those revisions. Do not infer
a license for vendored code from this protocol-only gate.
