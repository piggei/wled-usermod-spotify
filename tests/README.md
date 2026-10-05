# Release and hardware checks

Project-specific test definitions live in TSV files so the WLED update helper does not need to be edited when checks are added, removed, or updated.

## `release_checks.tsv`

Automatic checks run by `tools/test_runner.sh` in two phases:

- `prebuild`: source/package checks after the downloaded archive has been synchronized into the external usermod repository.
- `postbuild`: checks against the generated WLED `firmware.bin`.

Columns:

`ID`, `PHASE`, `LEVEL`, `TYPE`, `TARGET`, `EXPECTED`, `DESCRIPTION`.

`required` failures stop the release workflow. `optional` failures are reported but do not block it.

To add a new check using an existing test type, edit only the TSV file. The update script remains unchanged.

## `hardware_checks.tsv`

Manual real-hardware qualification matrix. It records the test type, procedure, expected result, observed result, and notes. It is intentionally not executed automatically by the update helper.

`hardware_checks.tsv` is the single current hardware qualification checklist.
Legacy free-form checklists are intentionally not kept in `tools/` so test
expectations have one authoritative location.
