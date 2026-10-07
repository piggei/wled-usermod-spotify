# dev.2n-r15 - bounded metadata identity and territory audit

Date: 2026-10-07. Parent: `v0.1.0-dev.2n-vorbis-r14`.

## Goal and experiment boundary

The October 7 source investigation found that the normal legacy metadata parser
never inspects Track.gid (1), Track.restriction (11) or Track.alternative (13).
The original RequestKey shape is correct, but its GID/file association has not
been measured against these fields on the affected Premium/NG account.

r15 is the observation stage of that investigation. It reads the already-received
Mercury metadata body with an independent diagnostic parser. It does NOT relink,
change the queue or RequestKey target, add metadata/network requests, retry keys,
change the session latch, or connect live media to AES/Vorbis. The subsequent
manual, bounded multi-identity key experiment is deferred until real metadata
shows which identity pairs are present. Do not interpret a successful parser test
as a successful AudioKey test.

## Implemented fields

`spotify/SpotifyMetadataAudit.*` implements a fixed-size protobuf reader for:

| Message | Fields used |
| --- | --- |
| Track | gid=1, restriction=11, file=12, alternative=13 |
| Restriction | numeric catalogue=1 (packed/unpacked), countries_allowed=2, countries_forbidden=3, type=4, catalogue_str=5 |
| AudioFile | file_id=1, format=2 |
| Track (presence/count only) | sale_period=14, earliest_live_timestamp=17, availability=19 |

Album and artist GIDs cannot overwrite the top-level track GID. Every alternative
retains its own GID and file list; files are never combined between tracks. A
GID-only alternative is retained as such: r15 does not invent its files or fetch
another body automatically.

Limits: 64 KiB body, 4,096 protobuf fields, eight direct alternatives, eight files
per track, one alternative nesting level, 1,024 bytes per country list. The
field budget is global, including packed catalogues. Excess alternatives/files
or nested alternatives are explicitly flagged; malformed lengths, wire types,
varints and duplicate-conflicting identifiers cannot produce a positive pair
verification. No metadata string/list or whole protobuf body is logged.

The full report is a member of the session object. Parsing uses a nothrow heap
workspace; neither the AP task nor the async web task gets a full report as an
automatic stack variable. The native x86-64 test measures 2,264 bytes/report,
168 bytes/summary and 76 bytes/track view; target ABI sizes can differ slightly.
Publication/small snapshots use short critical sections. Parsing and Arduino
String/JSON work happen outside those sections. The generation check detects a
track/session switch while serializing alternative rows. A failure of the new
parser does not alter the qualified parser/control/key/transport flow.

## Meaning of the diagnostics

`Metadata audit state=ok|limited|malformed|field-budget|too-large|no-memory|...`
reports parsing of the current body, with cumulative parse attempts/failures,
per-body byte/field counts and last/max wall time in microseconds. `pending` clears
old track data immediately; a new AP connection also invalidates the old report.

`Metadata identity requestedGid=... returnedGid=... same=yes|no|unknown` compares
the requested queue identity to the actual metadata Track.gid. `unknown` means
missing/conflicting/unusable evidence, not equality.

`Metadata territory country=NG catalogue=premium state=allowed|restricted|unknown`
uses the actual country and catalogue supplied by this AP session. It evaluates
only an explicit, supported streaming country rule applicable to that catalogue.
Country lists are checked as aligned uppercase two-byte codes (not substrings).
Both empty allowlists (restricted) and empty denylists (allowed) are distinguished
from an absent predicate. Missing country/catalogue/scope, unsupported fields/types,
malformed country lists, conflicting selectors or contradictory rules remain
unknown. Modern catalogue strings are preferred when present; explicit disagreement
with known legacy selectors is unknown. Legacy enum mapping in this revision is
implemented only for the known `premium` catalogue: SUBSCRIPTION/ALL match, the
other known enum catalogues do not. Other/custom enum-only catalogues remain unknown.

**`allowed` is NOT authorization to obtain a key.** No restrictions also yields
`unknown` in this observation-only parser. Sale periods, embargo dates, availability,
explicit-content policies, album licensing and the keymaster's own policy are not
fully evaluated. Presence/count evidence for Track fields 14/17/19 is exposed and
`authorization=unknown` is always shown. This conservative diagnostic classification
is not intended as a replacement for a complete Spotify availability implementation.

`Metadata alternatives seen=N kept=M ... relinking=not-applied` reports direct
alternatives. Each bounded `Metadata alternative index=0..7` row reports its GID,
file count, preferred retained file (format 1 first, otherwise first retained file),
territory evidence and limits. `selected=no` is deliberate. `kept=0` with `seen=0`
is a result, not a failure.

`AudioKey target source=queue-primary gid=... fileId=... pair=match|mismatch|unknown`
records the exact last candidate tuple sent by the normal RequestKey path, or the
current first candidate when the unchanged latch suppresses the scan. `sent=no`
therefore must NOT be counted as another key rejection. `pair=match` proves only
that this GID is the returned primary GID and the file occurs in that same primary
metadata object. It does not prove server authorization. A missing file outside
a truncated/unusable list yields unknown rather than a false mismatch.

The preferred primary/alternative file need not equal the last attempted candidate:
the existing scan may finish at AAC format 8 after trying the Vorbis formats.
Neither the candidate scan nor its ordering has changed.

## Hardware test

No override changes and no local beep are required for this gate.

1. Compile/flash r15 and transfer one track with the existing Premium/NG account.
2. After the usual canary completes, capture `/json/info`.
3. Select two other playlist tracks and capture `/json/info` after each one. The
   audit runs on each received metadata response even if RequestKey is suppressed.
4. Expect state `ok` or a clearly explained limitation, identity/territory/alternative
   evidence, and an exact target tuple. Do NOT require any particular alternative
   count, territory result or key success before seeing the actual bodies.
5. Check the regression counters: Shannon macFail=0, AP Stream 3/3 per track,
   liveVerify hashMatch/eof/rewind=yes, consumer/decrypt closed, no unexplained
   stack/heap collapse or resets. Standard key suppression remains in effect.

Evidence from step 3 characterizes metadata, NOT fresh key trials. If an allowed
alternative with its own files or a primary GID mismatch exists, the next build
can add a manual finite probe for that exact tuple while preserving SPIRC identity.
If no such evidence exists, do not manufacture an alternative or claim that this
hypothesis caused the observed 0:1.

## Host verification

Run the normal source suite:

```sh
./tools/test_runner.sh --manifest tests/release_checks.tsv --phase prebuild --repo .
```

The new native test requires a host C++11 compiler (`g++`, or set `CXX`). It compiles
and executes the actual `SpotifyMetadataAudit.cpp`; it is not a Python reimplementation.
It includes 48 named scenarios and 12,000 deterministic random/truncation/mutation
inputs. To repeat the separately run sanitizer check:

```sh
SPOTIFY_AUDIT_SANITIZERS=1 python3 tests/test_dev2n_r15_metadata_native.py
```

A second test checks the new adapter/UI syntax against the real SessionProbe
header using small Arduino/FreeRTOS/JSON stubs. This is explicitly NOT an ESP32
compile. A third test verifies byte hashes for 22 unchanged files and seven
unchanged r14 code blocks (hello/auth/RequestKey/StreamChunk/legacy parser/canary/
key response logic). Target compile, postbuild firmware checks and hardware
validation remain separate and pending at delivery.

## Reference sources rechecked for r15

No upstream implementation has been copied or vendored; this parser independently
implements schema field facts. Links were readable in the research browser on
2026-10-07; no complete GitHub clone or target toolchain was downloaded.

- Schema: https://raw.githubusercontent.com/librespot-org/librespot/dev/protocol/proto/metadata.proto
- Country/catalogue interpretation and distinct availability checks: https://raw.githubusercontent.com/librespot-org/librespot/dev/metadata/src/audio/item.rs
- Same-message GID/file choice before RequestKey: https://raw.githubusercontent.com/feelfreelinux/cspot/master/cspot/src/TrackQueue.cpp
- Alternative-selection precedent (not proof for this account): https://github.com/devgianlu/go-librespot/pull/267

These are moving upstream references, not pinned/vendored dependencies. Existing
PlatformIO dependencies and the micro-vorbis compatibility bridge are unchanged.
