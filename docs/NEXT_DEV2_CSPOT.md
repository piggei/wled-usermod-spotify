# Next milestone after dev.2f-r1

Do not change the qualified shared-I2S/ES8311/DMA/PCM path, the dev.2d LoginBlob
credential path, or the dev.2e AP resolver/TCP/handshake/Shannon authentication
path unless a regression requires it.

The dev.2f-r1 gate keeps the authenticated Shannon socket alive, handles AP
PING/PONG and country packets, sends the first Mercury remote-user SUB and adds
bounded reconnect plus duplicate-credential flash-write suppression. Once those
behaviors are qualified on hardware, the next milestone is the minimum Spotify
Connect control plane:

1. parse the remote-user Mercury subscription payloads needed by Connect;
2. add the minimum SPIRC protobuf/state required to announce the device;
3. send the initial SPIRC hello/notify state without touching the audio backend;
4. accept load/play/pause/seek/volume control messages and expose them only as
   diagnostics/callbacks in the first control-plane build;
5. qualify takeover, reconnect, memory and stack behavior before media decode.

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
