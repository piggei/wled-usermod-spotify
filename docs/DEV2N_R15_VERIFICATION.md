# r15 delivery verification

Baseline archive: `wled-usermod-spotify-v0.1.0-dev.2n-vorbis-r14.zip`.
Baseline archive SHA-256:
`64930a5b63c7518ad174de3e7048577d7b18859412397de745d395375e480b91`.

Executed in the preparation environment:

| Check | Result |
| --- | --- |
| Complete prebuild manifest | 192 PASS, 0 required failures, 0 optional failures |
| Native C++11 parser with warnings as errors | PASS, 48 scenarios + 12,000 deterministic mutation/truncation/random inputs |
| Same test with AddressSanitizer + UndefinedBehaviorSanitizer | PASS, no sanitizer diagnostic |
| Adapter/UI syntax against the real SessionProbe declarations, stubbed host platform | PASS; not a firmware compile |
| Unchanged file hashes vs r14 | 22/22 identical, including all audio/decoder source and fixture files |
| Frozen source blocks vs r14 | 7/7 identical: hello, auth, RequestKey, StreamChunk request, legacy metadata, candidate/canary flow, key response |
| WLED / PlatformIO target build | NOT RUN; complete target tree/toolchain unavailable here |
| Postbuild on r15 firmware ELF/BIN | NOT RUN; no r15 firmware produced here |
| Hardware | PENDING |

The source archive is not a compiled firmware binary. No replacement credentials,
raw Spotify metadata/media capture or real audio keys are included. Existing local
synthetic fixtures and their public synthetic key remain unchanged.

Most pre-existing prebuild checks are static contracts. The new parser test really
compiles and executes the exact C++ implementation; its fuzz corpus is synthetic
and does not claim to cover all Spotify metadata. The hardware matrix deliberately
leaves all r15 rows pending until user evidence is available.
