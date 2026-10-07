# dev.2n-r16 - manual primary/alternative AudioKey comparison

## Scope

r15 hardware observations on 2026-10-07 showed:

| Track | Requested/returned GID | Primary pair | Territory rule | Complete alternatives | Normal AudioKey attempt |
| --- | --- | --- | --- | --- | --- |
| Perfect Day - Chris Lord Alge Mix | Same | Match | Allowed for NG/premium | 2 | Four formats sent; four explicit 0:1 rejections |
| Close My Eyes Forever | Same | Match | Allowed for NG/premium | 1 | None: normal session latch suppressed it |
| Passion | Same | Match | Allowed for NG/premium | 0 | None: normal session latch suppressed it |

Audit errors were zero; last parsing times were 261/182/125 us. Metadata advanced
1/2/3 and AP canary requests 3/6/9, with matching source hashes and Shannon macFail=0.
These are observations on three supplied snapshots, not universal authorization
or a new qualification of the local Vorbis fixture.

r16 provides one explicit experiment on the current audited metadata: a primary
pair and at most two alternative pairs, all format 1 (OGG_VORBIS_160). It does not
perform automatic relinking, add a new authentication flow, or open live playback.
A rejected primary does not prevent the manual experiment on a later track.

## Start from a browser

1. Build/flash r16 using the same PlatformIO override. Select the first track in
   Spotify and wait for normal metadata/AudioKey/canary work to finish.
2. Open `http://<WLED-IP>/spotify-key-probe`. This GET only displays a form/status;
   opening or reloading the page sends no RequestKey.
3. Confirm the current GID/generation and `AP transport ready: yes`, then press
   **Start AudioKey comparison**. The button submits a form POST with the displayed
   metadata generation and GID. The HTTP 202 response means queued, not completed.
4. Wait for `/json/info` to show `AudioKey probe state=complete`. Normally this is
   a few seconds; a slow/unresponsive server may cause explicitly reported timeouts
   or a run deadline. Save the JSON before starting another diagnostic run.
5. Select the next track, let its normal canary finish, reload the diagnostic page,
   and press the button again. Wait at least 10 seconds between run starts.

The three r15 examples should offer 3, 2 and 1 targets respectively if Spotify
returns the same metadata. No beep/music is expected from this test.

Optional terminal equivalent (use the current generation, not a hardcoded example):

```sh
curl -X POST "http://<WLED-IP>/spotify-key-probe" \
  --data "action=start&generation=<CURRENT_METADATA_GENERATION>&gid=<CURRENT_REQUESTED_GID_HEX>"
```

Cancel through the page's **Cancel diagnostic** button, or form POST `action=cancel`.
GET `?action=start` is intentionally not an activation API. A duplicated POST or a
second attempt on the same GID receives HTTP 409 instead of sending more requests.

## Exact limits and admission rules

- Maximum 3 pairs/run, 4 accepted runs/boot, 12 diagnostic request attempts/boot.
- Once per requested track GID per boot; changing away and back does not reset it.
- At least 10,000 ms between run starts and 500 ms between candidate completions
  and the next send. No automatic retry of any pair.
- Response timeout 2,500 ms/request, run deadline 15,000 ms. A timeout is not an
  explicit 0:1 rejection. A socket write failure aborts the run and lets the normal
  AP reconnect machinery recover the connection.
- A cancelled queued run consumes its run/GID allowance. A partial/failed send
  consumes its request allowance. Session stop/reset/reconnect do not replenish
  these limits; a board reboot starts a fresh diagnostic budget.
- Require a live authenticated AP, completed normal metadata/key work and the
  verified 3-range canary. Diagnostics do not overlap a normal key request.
- Only `Status::Ok` metadata with a matching primary requested/returned GID is
  accepted. Require a nonzero, unambiguous GID/file, an explicit allowed territory
  result, untruncated files, and format 1. Unknown/restricted/invalid alternatives
  are skipped and counted. No files are invented for GID-only alternatives.
- The first two eligible unique alternative pairs are selected in metadata order;
  additional eligible alternatives are counted as limited, not requested.

The earliest-live flag is reported as presence only. The r15 availability/date
limitations still apply. `territory=allowed` is not complete licensing clearance:
`authorization=unknown` stays explicit and the key service is the authority for
its own grant/rejection. This experiment never changes account country or claims
another device identity.

## Ownership, concurrency and cancellation

`SpotifyAudioKeyProbe` is an allocation-free, platform-independent state machine.
The HTTP callback only queues a plan from the protected metadata snapshot. It
cannot send on the socket. Network sends execute on the existing AP task using
the unchanged `buildAudioKeyRequest()` and Shannon send function.

Diagnostics take the next sequence from the **same existing AP allocator** as
normal RequestKey calls. No special high-bit namespace or client identity is
introduced. The normal reported request/response counters remain separate, while
the internal next-sequence value naturally advances for every wire request.
Ownership is recorded as (AP epoch, sequence) in a fixed 12-entry receipt ledger.
Old/cancelled replies in that epoch are quarantined before the normal handler;
receipts from an earlier connection cannot swallow a normal reply if a manual
session reset later restarts the sequence counter. Sequence reuse in an epoch or
allocator wrap is refused. Budgets/once-per-GID history survive session reset.

The POST form binds both generation and GID, so an old page from a previous boot
cannot select an unrelated track even if its numeric generation is reused.
Every run binds metadata generation, AP session epoch, requested track GID and
immutable target pairs. A metadata/track change, session stop/disconnect/reconnect,
manual cancel or deadline closes pending/planned work. Replies for old/finished
sequences are counted as late and cannot become a success for a newer target.
Normal receive traffic is drained before diagnostic sends; timeout/deadline checks
also run while receive traffic is busy, not only during idle polling.

The metadata and diagnostic state share one mux. Critical sections only handle
bounded POD data, no String construction, I/O or decoder work. JSON copies small
snapshots, with a run-ID check for each result; if a new run replaces the report
while it is being rendered, the output explicitly requests a fresh snapshot.
Results belong to the latest diagnostic run and can remain visible after the
current track changes (`generation` versus `currentGeneration`).

## Successful responses do not open the decoder

A matching 0x0D response must be exactly 4 sequence bytes plus 16 key bytes. The
state machine records `outcome=accepted keyBytes=16` but never copies the key.
The AP adapter wipes the handled response vector using volatile stores, also for
late/duplicate responses. There is no diagnostic key file, getter, JSON value or
key cache. Diagnostic code does not call AES, Vorbis, PCM or the live-source consumer.

A 0x0E response must contain the sequence and the two error bytes. Malformed
responses are not classified as a successful key or a normal 0:1 rejection; they
abort the experiment explicitly. No diagnostic success can set `audioKeyBytes_`,
clear the normal service-block latch, or associate an alternative key with a
primary file. Normal key state and diagnostic evidence are intentionally separate.

## JSON interpretation

Expected shape, not a predicted server result:

```text
AudioKey probe state=complete run=1 generation=... currentGeneration=...
  targets=3 format=1 pending=no sent=3 responses=3 accepted=... rejected=...
  timeouts=0 protoErr=0 writeErr=0 reason=none
AudioKey probePolicy mode=manual maxTargets=3 runs=1/4 requests=3/12
  ... normalCounters=separate normalLatch=unchanged keyStorage=none consumer=closed
AudioKey probeContext requestedGid=... session=...
  authorization=unknown relinking=not-applied
AudioKey probeTarget index=0 source=primary alternative=-1 format=1 gid=... fileId=...
  ... outcome=rejected sent=yes seq=4 cmd=14 err=0:1 keyBytes=0 rtt=...ms keyStored=no
```

The command is shown numerically: 13 means 0x0D, 14 means 0x0E. The target result
names are the primary interpretation. `sent=yes` includes an attempted write,
so check `outcome=write-error` separately. `state=complete` only means every planned
pair reached a terminal response/timeout; it does NOT mean a key was granted.

Existing `AudioKey requests/responses/ok` still refer only to the normal path.
They can remain 4/4/0 while the separate probe adds three real wire requests.
Global AP `rx/tx` naturally includes the additional traffic. `AP Stream`, selected
TrackRef/metadata, source hashes and local PCM counters are not changed by the
experiment. With the current normal 0:1 latch, `keyGate=blocked` remains possible
and expected even if a discarded diagnostic key is accepted.

## Decisions after hardware evidence

- Primary rejected / alternative accepted: evidence for identity-dependent key
  eligibility; preserve queue identity and design any relinking separately.
- A later primary accepted: evidence that extrapolating the first track's rejection
  through the normal latch hides other usable identities.
- All tested pairs explicitly rejected: stronger account/session/backend evidence,
  not proof of one universal cause or a verdict on every track in the catalogue.
- Timeout, malformed data, unavailable metadata, cancellation or budget refusal:
  report that condition, do not count it as a key rejection or playback success.

Even a key grant does not qualify a full Spotify track. Live key/file correlation,
media prefix, sustained transport/backpressure, real EOF, seeking and long-running
playback remain independent gates. The 2-second synthetic fixture is not evidence
that those have already been solved.

## Test and build discipline

Native tests compile the exact state machine and frozen wire builder, plus actual
adapter/HTTP/JSON helpers under host platform stubs. They test limits, ownership,
timeouts, wraparound, cancellation, response wiping and isolation. They are not
an ESP32 build, AP network test or proof of a real key grant.

```sh
./tools/test_runner.sh --manifest tests/release_checks.tsv --phase prebuild --repo .
SPOTIFY_PROBE_SANITIZERS=1 python3 tests/test_dev2n_r16_key_probe_native.py
SPOTIFY_PROBE_SANITIZERS=1 python3 tests/test_dev2n_r16_adapter_native.py
```

Then compile WLED and run its postbuild checks on the resulting ELF/BIN. The new
postbuild strings are also source-checked to avoid a stale-marker false failure.
See `DEV2N_R16_VERIFICATION.md` for what was actually run for this delivery.
