# Release and hardware checks

Project-specific test definitions live in TSV files so the WLED update helper does not need to be edited when checks are added, removed, or updated.

## `release_checks.tsv`

Automatic checks run by `tools/test_runner.sh` in two phases:

- `prebuild`: source/package checks after the downloaded archive has been synchronized into the external usermod repository.
- `postbuild`: checks the generated PlatformIO firmware artifacts, preferring sibling `firmware.elf` and falling back to `firmware.bin`.

Columns:

`ID`, `PHASE`, `LEVEL`, `TYPE`, `TARGET`, `EXPECTED`, `DESCRIPTION`.

`required` failures stop the release workflow. `optional` failures are reported but do not block it.

To add a new check using an existing test type, edit only the TSV file. The update script remains unchanged.

## `hardware_checks.tsv`

Manual real-hardware qualification matrix. It records the test type, procedure, expected result, observed result, and notes. It is intentionally not executed automatically by the update helper.

`hardware_checks.tsv` is the single current hardware qualification checklist.
Legacy free-form checklists are intentionally not kept in `tools/` so test
expectations have one authoritative location.


### dev.2f-r1 / dev.2g-r1
`test_dev2f_session_contract.py` retains the persistent Shannon keepalive and Mercury envelope/subscription regression. `test_dev2g_spirc_contract.py` adds the independent SPIRC field-number, Mercury SEND multipart-envelope, Hello and remote-frame-decode contract. Real device activation/Load remains an explicit hardware gate in `hardware_checks.tsv`.


### dev.2h-r1

`test_dev2h_metadata_contract.py` guards selected TrackRef extraction, `hm://metadata/3/track/<gid>` Mercury GET, legacy track/album/artist/cover/audio-file parsing, metadata telemetry, and the explicit absence of audio-key/CDN/decode work from this gate.

### dev.2i-r2

`test_dev2i_audio_key_contract.py` preserves the r1 RequestKey/AesKey wire contract and adds source guards for the bounded AudioFile candidate set, single-step candidate advance, track-change cancellation, stale-response handling and no-raw-key telemetry. Real candidate ordering/results remain a hardware gate in `hardware_checks.tsv`.

### dev.2j-r2

`test_dev2j_media_head_contract.py` guards AP ProductInfo `0x50` handling, bounded
`head-files-url` consumption, the 4 KiB range cap, explicit HTTPS classification,
no secure-client-header regression, and redacted media-head telemetry. Real
ProductInfo scheme/status/body evidence remains a hardware gate in
`hardware_checks.tsv`.

### dev.2l-r2 manifest synchronization

`test_dev2l_ap_stream_contract.py` also guards the postbuild firmware scope literals in `release_checks.tsv` against the runtime `/json/info` scope string. This prevents a stale release-manifest literal from producing a false required FAIL after an otherwise successful firmware build/flash.

### dev.2m-r1 media-key service-block hardening

`test_dev2m_key_block_contract.py` guards the narrow session latch: only an all-candidate `0x0e / 0:1` scan may mark the media key as service-blocked. Later tracks in the same started session suppress redundant RequestKey scans while keeping metadata/SPIRC and the already-qualified AP StreamChunk canary active. A fresh manual/auto session start clears the latch so service recovery can be re-evaluated.


### dev.2m-r12 canonical queue identity

`test_dev2m_r12_queue_identity.py` captures the real r11 GID/URI evidence with the observed GID `082fb9e25e8e48a79caa41a924af1574`, verifies that the qualified base62 conversion yields `spotify:track:0frKt739Ov9vvKS3JRu5Vi`, and models the 18-byte field-1-only TrackRef shape implied by `82 refs / 1476 bytes`. Source guards require GID-only TrackRefs to be canonicalized before URI matching, reject naked context `track_index` selection, and ensure bounded multi-skip JSON convergence runs before any first-target shortcut.


### dev.2m-r13 timing telemetry

`test_dev2m_r13_latency_telemetry.py` guards the measurement-only instrumentation added after r12 hardware-qualified direct selection. It requires local resolver/apply microsecond timing, metadata Mercury RTT timing, and an explicit `scan=suppressed` label for post-latch tracks. The original r13 hardware measurement used the then-frozen 250 ms poll.


### dev.2m-r14 conservative AP receive poll

`test_dev2m_r14_poll_optimization.py` guards the only scheduling change in r14: the idle AP receive poll is reduced from 250 ms to 50 ms while the r12 selection semantics and r13 timing instrumentation remain unchanged. The hardware gate is stability-first; latency improvement is desirable but not required.

### dev.2n-r7 local Ogg/Vorbis gate

`test_dev2n_vorbis_fixture_contract.py` guards the new decoder-only workstream. It reconstructs the checked-in synthetic fixture, verifies the Ogg/Vorbis identification/setup/comment headers and EOS page, confirms the 44.1 kHz stereo contract, requires the explicit `esphome/micro-vorbis ^0.1.0` manifest plus WLED-env dependency wiring, private include bridging, and bundled micro-ogg source bridging and verifies that decoded PCM is routed only through `enqueuePcm44100()`. r6 has now proven the target compile/link and audible decode path on hardware. r7 additionally guards the completion semantic learned from that run: exact full-fixture input exhaustion is a valid completion even if the last audio-producing call returns normal success rather than a separate EOS result, while explicit EOS remains classified separately.

### dev.2n-r8 4096-byte chunked-input gate

`test_dev2n_r8_chunked_stream_contract.py` preserves the hardware-qualified r7 contiguous path and adds the transport-shaped local replay. It requires `start-vorbis-chunked`, 4096-byte source deliveries, an 8192-byte staging buffer with tail compaction, chunk/refill telemetry, and strict full-input/full-frame completion. The host test reconstructs the 10,437-byte fixture and proves it splits exactly as 4096 + 4096 + 2245 bytes; it also verifies that at least two Ogg pages cross 4096-byte boundaries, so the hardware gate genuinely exercises incremental page/packet reconstruction rather than page-aligned chunks.

### dev.2n-r9 AES-CTR gate

`test_dev2n_r9_aes_ctr_pipeline.py` validates the local encrypted-fixture contract without third-party Python packages. It checks the exact plaintext/ciphertext fixture hashes, the first-block CTR keystream oracle, the fixed legacy Spotify audio IV in `SpotifyAudioAesCtr`, the mbedTLS AES-128-CTR implementation, the 4096-byte chunk adapter, the AES test endpoint and crypto telemetry markers. The fixture key is synthetic/public and is not a Spotify AudioKey.

### dev.2n-r10 macro-collision gate

`test_dev2n_r10_aes_macro_safe.py` prevents reuse of the ESP32-S3 HAL macro token `IV_BYTES` as a C++ member after including mbedTLS AES. It requires the macro-safe `kIvBytes` / `kKeyBytes` identifiers while preserving the r9 AES-CTR telemetry contract.


### dev.2n-r11 MediaChunkSource / AP pre-decrypt gate

`test_dev2n_r11_media_source_contract.py` preserves the bounded `SpotifyMediaChunkSource` producer API, verifies that both plaintext and encrypted local chunked fixtures still use it, and keeps the independent AP rolling hash/byte/chunk-shape source-gate telemetry as a regression baseline. r12 may add a separate transient encrypted source, but this r11 gate must remain intact.

### dev.2n-r12 transient AP MediaChunkSource / hard key fence

`test_dev2n_r12_ap_buffered_source_contract.py` requires `SpotifyApMediaChunkSource` to implement the common `SpotifyMediaChunkSource` API with a fixed 3 x 4096-byte capacity, lazy PSRAM-preferred allocation, wipe-on-reset/invalidate semantics, fragment append/commit handling, and no key material. It also verifies that `SpotifySessionProbe` only fills the encrypted source from the qualified AP canary and that the live source is not connected to `SpotifyAudioAesCtr` or `SpotifyVorbisFixturePlayer`. `/json/info` must expose only counters/storage class plus `consumer=closed keyGate=...`.

### dev.2n-r13 live source diagnostic consumer

`test_dev2n_r13_live_source_verify_contract.py` requires the retained AP canary to be read only through the common `MediaChunkSource::next()` contract, compared against the independent rolling AP sourceGate by byte count/chunk count/FNV identity, and rewound afterward without opening AES/Vorbis. `/json/info` exposes only bounded counters/digest equality plus `decrypt=closed`; the r12 media consumer fence remains in place.

### dev.2n-r14 AudioKey identity telemetry

`test_dev2n_r14_audiokey_identity_telemetry.py` guards the measurement-only identity audit after r13 hardware-qualified the live `MediaChunkSource` consumer. It requires the previously qualified ClientHello/Auth identity values to remain unchanged, adds only bounded ProductInfo key/capability parsing, keeps the 42-byte RequestKey layout frozen, and proves the live AES/Vorbis consumer fence remains closed.


## r16 manual key diagnostics

`test_dev2n_r16_key_probe_native.py` compiles the actual allocation-free probe and
extracts the unchanged RequestKey builder for execution. It covers plan bounds,
strict format/identity checks, deadlines, sequence ownership, duplicate requests,
cancellation, immutable tuple results and deterministic event interleavings.
`test_dev2n_r16_adapter_native.py` executes the actual AP adapter and HTTP/JSON
helper bodies with platform stubs; response buffers are checked for secure wiping
and normal key counters/latch are checked for non-mutation. The host String stub
asserts that no String allocation occurs inside the mux-protected sections.
Set `SPOTIFY_PROBE_SANITIZERS=1` to enable ASan/UBSan on either native suite.
These tests do not compile full WLED, run FreeRTOS scheduling, contact Spotify or
prove hardware behavior. `test_dev2n_r16_probe_contract.py` additionally verifies
frozen r14/r15 files/blocks, routing, limits, and current postbuild literals.
