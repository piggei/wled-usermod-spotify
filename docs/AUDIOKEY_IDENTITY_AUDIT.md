# AudioKey session-identity audit

## Status

The Spotify AES-key problem remains open but is **not** being modified in dev.2n-r7. The current Premium account completes Connect transfer, AP authentication, Shannon, Mercury/SPIRC, metadata and encrypted AP StreamChunk, but every AudioFile candidate receives `AesKeyError 0:1`. The same account also reproduced the same class of failure with librespot 0.8.0.

The available non-Premium account cannot provide a RequestKey A/B comparison: Spotify discovers `WLED Matrix` but disables it in the Connect picker with “Passa a Spotify Premium per ascoltare”. That transfer is blocked before this usermod reaches the AP/SPIRC/AudioKey experiment. Alexa endpoints visible to that account use a different service integration and are not a model to impersonate.

## Current WLED AP identity fields - freeze for dev.2n

The current source sends these project-local identity values during the already-qualified AP path:

- ClientHello Spotify version marker: `0x10800000000`;
- BuildInfo product: `PRODUCT_CLIENT` (`0`);
- BuildInfo platform: `PLATFORM_LINUX_X86` (`2`);
- authentication SystemInfo CPU: `CPU_UNKNOWN` (`0`);
- authentication SystemInfo OS: `OS_UNKNOWN` (`0`);
- system-information string: `wled-spotify`;
- client version string: `wled-spotify-dev2e-r2`;
- stored reusable credential authentication is used by the hardware-qualified session.

These values already authenticate successfully, so they must not be changed speculatively. A RequestKey rejection may depend on service/account/session policy rather than the 42-byte request itself.

## Evidence-driven comparison plan

Before changing the ESP32 identity or RequestKey wire:

1. run a current desktop librespot build with the same Premium account and capture only non-secret AP/session classifications and the final AudioKey outcome;
2. compare ClientHello BuildInfo, AP login SystemInfo/product/platform/version class, credential type and any ProductInfo/capability attributes that can be observed without exposing credentials/tokens;
3. identify the **smallest single identity difference** that has protocol evidence behind it;
4. change one field at a time in a dedicated diagnostic build;
5. preserve the qualified RequestKey layout (`fileId[20] + raw GID[16] + BE32 seq + BE16 zero`) unless a byte-level comparison disproves it;
6. never log or serialize credentials, access tokens or a successful AES key.

## Important interpretation

A non-Premium UI restriction and the Premium-account `AesKeyError 0:1` occur at different layers. The Free-account screenshot is therefore evidence against treating `0:1` as simply the normal Connect Premium entitlement check.

Likewise, the ability of Alexa endpoints to appear/select under a different account policy does not establish that Alexa exposes reusable Spotify key material to a local receiver. No Alexa impersonation or entitlement bypass belongs in this project.

## Parallel work

The local Vorbis workstream can proceed independently. If AudioKey becomes available later, the intended final join remains:

```text
AP StreamChunk encrypted bytes
        -> Spotify media AES decrypt
        -> bounded Ogg/Vorbis streaming decoder
        -> signed 16-bit stereo 44.1 kHz
        -> WavesharePcmOutput::enqueuePcm44100()
```
