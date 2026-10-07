# Continuation plan after the qualified r14 baseline

## Qualified state entering dev.2n

The network/control path is sufficiently characterized and should remain frozen while decoder work proceeds:

- dev.2i: RequestKey is mechanically correlated but the current Premium account returns `0x0e / 0:1` for all AudioFile candidates; the same class of error was independently reproduced with librespot 0.8.0;
- dev.2l: encrypted media bytes are reachable through AP StreamChunk; three sequential 4 KiB ranges are qualified;
- dev.2m-r12: direct playlist taps are hardware-qualified through `skip_to.track_uid -> URI -> canonical GID-only TrackRef`;
- r13: local resolved Load-to-Notify work measures only about 45-47 ms;
- r14: the 50 ms AP receive poll is hardware-qualified with no Shannon, StreamChunk, WLED FPS or audio-sink regression.

Do not change shared-I2S/ES8311/DMA/PCM, LoginBlob, AP/Shannon, Mercury/SPIRC, metadata, RequestKey wire/correlation, direct-selection mapping, AP StreamChunk or the r14 poll without new evidence.

## Active gate: dev.2n-r7 local Ogg/Vorbis

`dev.2n-r7` starts from the now-proven r6 target integration: WLED/Arduino compiles and links micro-vorbis plus the bundled micro-ogg demuxer, and real hardware produced clean decoded audio. r7 changes only fixture completion bookkeeping: full caller-input exhaustion is accepted when the complete known fixture, valid format and exact 88,200-frame oracle all agree; explicit EOS remains separately telemetered. It still feeds only `WavesharePcmOutput::enqueuePcm44100()`. See `DEV2N_LOCAL_VORBIS.md`.

Required order:

1. re-run one-shot local fixture on r7 and confirm `complete=1 errors=0`, exact 88,200 frames and the new `eos/eof` classification;
2. confirm a single invocation reports `starts=1` (r6 evidence suggests the test URL may have been invoked twice in that boot);
3. regress the r14 Spotify path after fixture playback;
4. only after PASS, convert the local decoder producer to bounded chunked/streaming input using the same fixture.

## Parallel AudioKey investigation

Keep AudioKey as a separate evidence-gathering track. The second non-Premium account is not usable as a RequestKey A/B control because Spotify blocks WLED selection before transfer. Compare the current AP/session identity against a current desktop client before changing any identity field or RequestKey bytes. See `AUDIOKEY_IDENTITY_AUDIT.md`.

## Deferred join

Only when a legitimate session supplies a valid media key should the pipeline be joined:

```text
AP StreamChunk encrypted bytes
        -> AES decrypt
        -> bounded Ogg/Vorbis stream decoder
        -> 44.1 kHz signed 16-bit stereo PCM
        -> WavesharePcmOutput::enqueuePcm44100()
```

No alternate key derivation, entitlement bypass or Alexa impersonation is part of the design.
