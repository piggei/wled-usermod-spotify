# Continuation after dev.2n-r17

## Current gate: 64 KiB encrypted transport

r16 hardware sent six concrete primary/alternative AudioKey probes and all were
explicitly rejected `0:1`. A fresh upstream librespot `dev` build on Windows, same
Premium account/country/AP, independently requested the same primary GID/file pairs
and also received `0:1`; additional automatically advanced tracks failed the same
way. Current go-librespot master exposes a PlayPlay interface but its public plugin
remains unsupported/stubbed, so no public compatible license provider is available
to integrate here.

r17 therefore freezes RequestKey and advances only sustained encrypted transport:
16 x 4096-byte sequential AP ranges through a bounded PSRAM-preferred ring and an
independent hash-only consumer. Live AES/Vorbis remains disconnected.

## Decision after r17 hardware

If the 64 KiB pass is clean on at least two tracks, treat AP encrypted-byte transport
as qualified for the next media-plumbing step. Do not reopen AudioKey without new
external evidence (upstream fix, legitimate compatible licensing path, or a known
working independent client on this same account).

The next safe engineering work after a clean r17 is transport lifecycle rather than
DRM: real file-length termination, cancellation/seek behavior, longer sustained
windows and producer/consumer scheduling. A live decrypt/decoder join remains gated
on a legitimately returned usable key.

## Qualified components that remain frozen

- dev.2m-r14: canonical GID-only queue selection and 50 ms AP receive poll.
- dev.2n-r7/r8/r10: local contiguous/chunked/AES-CTR fixture to PCM and ES8311.
- dev.2n-r11/r12/r13: producer contract, retained 12 KiB AP canary and read/hash/rewind.
- dev.2n-r14/r15/r16: identity audit, metadata restrictions/alternatives and bounded manual key comparison.
- legacy RequestKey serialization and normal AudioKey latch remain frozen after the independent librespot reproduction.

## Deferred

No PlayPlay deobfuscator, identity impersonation, entitlement bypass, live Spotify
AES decrypt or decoder feed is introduced in r17.
