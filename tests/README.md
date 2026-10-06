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
