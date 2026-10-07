# dev.2n-r17 verification

## Host qualification completed

The r17 source tree passes the complete data-driven prebuild manifest:

- 204 required checks PASS;
- 0 required failures;
- 0 optional failures.

The actual `SpotifyApContinuousRing` C++ implementation was compiled and executed
with GCC and Clang on the host. Behavioral coverage includes the exact 65,536-byte
r17 sequence, fragmented producer writes, continuous consumer draining,
wrap-around, capacity backpressure without overwrite, offset gaps, duplicate /
backward offsets and premature producer completion.

The ring test also passed AddressSanitizer + UndefinedBehaviorSanitizer. The r16
AP adapter / HTTP helpers were re-run with ASan/UBSan after adding the new ring
member to `SpotifySessionProbe`.

## Frozen-path verification

`tests/fixtures/r16_frozen_paths.json` records the delivered r16 archive hash and
byte hashes for the r16 AudioKey probe / metadata-audit translation units. It also
freezes these `SpotifySessionProbe.cpp` blocks from r16:

- normal metadata/track request path;
- normal AudioKey sender;
- normal AudioKey timeout/candidate path;
- qualified canary StreamChunk receiver;
- normal AudioKey response handler.

The r17 contract test verifies those hashes before checking the new continuous
transport integration. Older r14/r15 freeze manifests remain active as well.

## Not claimed by host tests

No complete WLED/ESP32 target tree or target toolchain is available in this
preparation environment. Therefore r17 still requires:

1. `platformio run -e waveshare_spotify` in the normal WLED workspace;
2. postbuild `release_checks.tsv` against the produced firmware;
3. hardware `/json/info` evidence for at least two tracks.

Host qualification does not claim AP network interoperability of the new phase;
that is the purpose of the r17 hardware gate.
