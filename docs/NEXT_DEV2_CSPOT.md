# Continuation after dev.2n-r18

## Current gate: SPIRC position-boundary correction

r16 hardware sent six concrete primary/alternative AudioKey probes and all were
explicitly rejected `0:1`. A fresh upstream librespot `dev` build on Windows, same
Premium account/country/AP, independently requested the same primary GID/file pairs
and also received `0:1`; additional automatically advanced tracks failed the same
way. Current go-librespot master exposes a PlayPlay interface but its public plugin
remains unsupported/stubbed, so no public compatible license provider is available
to integrate here.

r17 hardware is now clean on two tracks: both 64 KiB sequences completed 16/16 with
matching producer/consumer hashes, zero transport/order errors and PSRAM high-water of
4096 bytes. AP encrypted-byte transport is therefore qualified at that bounded window.

r18 corrects an independent SPIRC UI/control issue observed during that test: a direct
new-track selection could inherit the previous track's running `positionMs`. Changed
identities now reset to zero, while initial ownership transfer and current-track
Pause/Resume/Seek keep their appropriate position semantics.

## Decision after r18 hardware

After confirming the new track starts near zero and Pause/Resume/Seek do not regress,
the next safe engineering work is transport lifecycle rather than DRM: real file-length
termination, cancellation/seek behavior, longer sustained windows and producer/consumer
scheduling. Do not reopen AudioKey without new external evidence. A live decrypt/decoder
join remains gated on a legitimately returned usable key.

## Qualified components that remain frozen

- dev.2m-r14: canonical GID-only queue selection and 50 ms AP receive poll.
- dev.2n-r7/r8/r10: local contiguous/chunked/AES-CTR fixture to PCM and ES8311.
- dev.2n-r11/r12/r13: producer contract, retained 12 KiB AP canary and read/hash/rewind.
- dev.2n-r14/r15/r16: identity audit, metadata restrictions/alternatives and bounded manual key comparison.
- legacy RequestKey serialization and normal AudioKey latch remain frozen after the independent librespot reproduction.

## Deferred

No PlayPlay deobfuscator, identity impersonation, entitlement bypass, live Spotify
AES decrypt or decoder feed is introduced in r18.
