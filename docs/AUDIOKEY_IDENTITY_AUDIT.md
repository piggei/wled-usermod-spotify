# Current scope update - dev.2n-r17

r16 hardware sent all six planned concrete primary/alternative probes; every one
returned `AesKeyError 0:1`. A fresh upstream librespot `dev` build on Windows then
authenticated the same Premium account, reported country NG, selected the same
primary GID/file pairs for the three controlled tracks, and received the same `0:1`
responses. It also failed on additional automatically advanced tracks. This removes
the WLED RequestKey builder and the three chosen track identities as the leading
explanations, without claiming a universal Spotify backend rule.

A current go-librespot master audit also found the public PlayPlay plugin still
unsupported/stubbed (`IsSupported() == false`, no token, deobfuscation returns
`playplay plugin not provided`), so r17 does not attempt a PlayPlay integration.
RequestKey/AP identity/country remain frozen pending new external evidence.

r17 advances only bounded encrypted transport; see
[DEV2N_R17_CONTINUOUS_AP_STREAM.md](DEV2N_R17_CONTINUOUS_AP_STREAM.md).

# Current scope update - dev.2n-r15

The r14 hardware snapshot confirms the identity telemetry was emitted and the
same four 0:1 rejections occurred. It does not establish a universal backend cause.
The October 7 source comparison found a separate missing check: primary metadata
GID, country/catalogue restrictions and alternatives. r15 implements that audit,
without replacing the key target or changing retries. See
[DEV2N_R15_METADATA_AUDIT.md](DEV2N_R15_METADATA_AUDIT.md).

A valid Premium ProductInfo is not per-track key authorization. Likewise, a
same-message GID/file match is not proof that the keymaster must accept it. The
phrase `service-blocked` is a local latch classification, not an official cause
returned by Spotify. The historical identity investigation follows below.

# AudioKey session-identity audit

## Status

The Spotify AES-key problem remains open but is **not** being modified in dev.2n-r9. The current Premium account completes Connect transfer, AP authentication, Shannon, Mercury/SPIRC, metadata and encrypted AP StreamChunk, but every AudioFile candidate receives `AesKeyError 0:1`. The same account also reproduced the same class of failure with librespot 0.8.0.

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

## dev.2n-r14 measurement-only telemetry

Hardware r13 proved that the real AP encrypted canary can be read through the common `MediaChunkSource` consumer contract with exact byte/chunk/hash identity and rewound safely, while the live decrypt consumer remains closed. The next useful work is therefore no longer media plumbing; it is evidence collection around the remaining AudioKey rejection.

r14 does **not** change ClientHello, AP authentication SystemInfo, stored-credential auth, ProductInfo handling semantics, RequestKey bytes, candidate ordering, retry policy, StreamChunk, AES/Vorbis, or the audio sink. It only exposes the already-sent non-secret AP identity classifications and a bounded set of ProductInfo key-related capability tags so they can be compared with a desktop librespot run of the same account.

Expected diagnostic lines:

```text
AP identity product=0 platform=2 spotifyVersion=0x10800000000 cpu=0 os=0 system=wled-spotify clientVersion=wled-spotify-dev2e-r2 authType=<n> mode=measurement-only
ProductInfo keyCaps onDemand=<v|missing> highBitrate=<v|missing> unrestricted=<v|missing> mobile=<v|missing> prefetchKeys=<v|missing> keyMemory=<v|missing> keyCacheMax=<v|missing>
AudioKey identityAudit requestWire=frozen fileId20+gid16+be32seq+be16zero identityMutation=none
```

The additional ProductInfo values are treated only as service capability evidence. They are not authorization tokens and are not used to modify requests. No credentials, access tokens, raw ProductInfo XML, AudioKey bytes or media bodies are exposed.

Current public librespot evidence remains consistent with an account/service-side dimension: issue #1649 tracks `audio key 0 1`, and issue #1735 reports the same current librespot binary succeeding for one Premium account and failing for another on the same machine/network/track. This makes measurement-first identity/capability comparison more defensible than speculative RequestKey mutation.
