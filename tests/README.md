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
