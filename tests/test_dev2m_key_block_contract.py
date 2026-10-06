#!/usr/bin/env python3
from pathlib import Path
import json

root = Path(__file__).resolve().parents[1]
lib = json.loads((root / "library.json").read_text())
h = (root / "spotify/SpotifySessionProbe.h").read_text()
cpp = (root / "spotify/SpotifySessionProbe.cpp").read_text()
ui = (root / "usermod_spotify_connect.h").read_text()
manifest = (root / "tests/release_checks.tsv").read_text()

assert lib["version"] == "0.1.0-dev.2m-key-block-r14"
assert 'USERMOD_VERSION = "0.1.0-dev.2m-key-block"' in ui
assert 'USERMOD_REVISION = "r14"' in ui

# Latch is deliberately narrow: only a complete candidate scan where every result is 0x0e/0:1.
assert "allRejected01" in cpp
assert "audioKeyCandidateResultCommand_[i] != AES_KEY_ERROR_COMMAND" in cpp
assert "audioKeyCandidateError0_[i] != 0u" in cpp
assert "audioKeyCandidateError1_[i] != 1u" in cpp
assert 'setError("Spotify media key service-blocked (0:1)")' in cpp
assert "mediaKeyServiceBlocked_ = true" in cpp
assert "++mediaKeyBlockEvents_" in cpp

# Once latched, later tracks in the same started session do not re-run RequestKey.
assert "if (mediaKeyServiceBlocked_)" in cpp
assert "++mediaKeySuppressedTracks_" in cpp
assert 'setError("Spotify media key service-blocked; RequestKey suppressed")' in cpp
assert "sendAudioKeyCandidate(0u)" in cpp
assert "startApStreamCanary()" in cpp

# A fresh start clears the latch; AP auto-reconnect does not, avoiding retry storms.
start = cpp.index("bool SpotifySessionProbe::startNow")
run = cpp.index("bool SpotifySessionProbe::runOneSession")
segment = cpp[start:run]
assert "mediaKeyServiceBlocked_ = false" in segment
run_segment = cpp[run:]
assert "mediaKeyServiceBlocked_ = false" not in run_segment

# Telemetry is explicit and contains no raw key.
assert "MediaKey state=" in ui
assert "suppressedTracks=" in ui
assert "blockErr=" in ui
assert "audioKeyHex" not in ui
assert "service-blocked" in h

scope = "scope=metadata -> one RequestKey diagnostic scan per session -> media-key service-block latch/suppression -> qualified AP StreamChunk canary; decrypt/decoder remain closed"
assert scope in ui
assert manifest.count(scope) >= 3

# r3 baseline retained: current-SPIRC Notify state mirrors repeated field-27 TrackRef queue.
assert 'kBase62[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"' in cpp
assert 'String(F("spotify:track:")) + encoded' in cpp
builder_start = cpp.index('std::vector<uint8_t> buildSpircTransferNotify')
builder_end = cpp.index('std::vector<uint8_t> buildMercuryRequest', builder_start)
builder = cpp[builder_start:builder_end]
assert 'appendStringField(state, 1u, trackUri)' not in builder
assert 'appendVarintField(state, 9u' not in builder
assert 'appendMessageField(state, 27u, trackRef)' in builder
assert 'for (const auto& trackRef : remote.trackRefs)' in builder
assert 'trackRefs.emplace_back' in cpp
assert 'MAX_SPIRC_STATE_TRACK_REFS = 96u' in cpp
assert 'MAX_SPIRC_STATE_TRACK_REF_BYTES = 12288u' in cpp
assert 'spircStateTrackRefs_ = remote.trackRefs' in cpp
assert 'current.trackRefs = spircStateTrackRefs_' in cpp
assert 'current.trackRefsTruncated = spircStateTrackRefsTruncated_' in cpp
assert 'SPIRC state tracks=' in ui
assert 'trackBytes=' in ui and 'fallbackRefs=' in ui

# r4 command-plane gate: classic SPIRC command types, targeted recipient filtering and command ACKs.
assert 'SPIRC_PLAY_PAUSE = 0x17u' in cpp
assert 'SPIRC_SEEK = 0x18u' in cpp
assert 'SPIRC_PREV = 0x19u' in cpp
assert 'SPIRC_NEXT = 0x1Au' in cpp
assert 'addIntCapability(10u, 1)' in cpp  # kCommandAcks
assert 'frameField == 18u' in cpp and 'info.recipients.push_back' in cpp
assert 'extractProtoVarint(data.data(), data.size(), 4u, seqNr)' in cpp
assert 'appendStringField(state, 20u, remote.lastCommandIdent)' in builder
assert 'appendVarintField(state, 21u, remote.lastCommandMsgid)' in builder
assert 'if (!self && !addressedToUs)' in cpp
assert '++spircRecipientIgnored_' in cpp
assert 'rememberSpircCommandAck(info)' in cpp
assert '++spircCommandAcksSent_' in cpp

# r4 handles the core controller commands instead of leaving the phone to retry/reconcile forever.
assert '} else if (info.type == SPIRC_PLAY_PAUSE)' in cpp
assert '} else if (info.type == SPIRC_SEEK)' in cpp
assert '} else if (info.type == SPIRC_NEXT || info.type == SPIRC_PREV)' in cpp
next_block_start = cpp.index('} else if (info.type == SPIRC_NEXT || info.type == SPIRC_PREV)')
next_block_end = cpp.index('              state_ = State::SpircReady;', next_block_start)
next_block = cpp[next_block_start:next_block_end]
assert '++spircNextFrames_' in next_block
assert '++spircPrevFrames_' in next_block
assert 'sendSpircControlNotify(nextState)' in next_block
assert 'sendTrackMetadataRequest(nextState)' in next_block

# Duplicate/empty Load retries are now actively ACKed with the retained coherent state.
load = cpp.index('} else if (info.type == SPIRC_LOAD)')
play = cpp.index('} else if (info.type == SPIRC_PLAY)', load)
load_block = cpp[load:play]
assert '++spircEmptyLoadsIgnored_' in load_block
assert '++spircDuplicateLoadsIgnored_' in load_block
assert 'sendSpircControlNotify(current)' in load_block
assert '++spircDuplicateLoadsAcked_' in load_block
assert load_block.index('++spircEmptyLoadsIgnored_') < load_block.index('sendSpircTransferNotify(info)')
assert load_block.index('++spircDuplicateLoadsIgnored_') < load_block.index('sendSpircTransferNotify(info)')

# r5: key unavailability must not force the controller back to Pause.
# The blocked Notify preserves the current UI play status and position.
assert 'auto currentSpircPositionMs' in cpp
assert 'auto setSpircPlaybackClock' in cpp
assert 'spircPlaybackClockRunning_ = status == 1u' in cpp
blocked_start = cpp.index('auto sendSpircBlockedNotify')
blocked_end = cpp.index('auto rememberSpircCommandAck', blocked_start)
blocked = cpp[blocked_start:blocked_end]
assert 'current.playStatus = (spircLastLoadStatus_ == 1u) ? 1u : 2u' in blocked
assert 'current, current.playStatus, metadataDurationMs_' in blocked
assert 'current.playStatus = 2u' not in blocked

# r5 Play is a UI/control command even when media-key service is blocked.
play_start = cpp.index('} else if (info.type == SPIRC_PLAY)')
pause_start = cpp.index('} else if (info.type == SPIRC_PAUSE)', play_start)
play_block = cpp[play_start:pause_start]
assert 'mediaKeyServiceBlocked_ ? 2u : 1u' not in play_block
assert 'makeRetainedSpircState(trackRefIndex_, 1u, currentSpircPositionMs())' in play_block

# Direct queue selection may be encoded in Play state indices without a new Load.
assert 'info.hasPlayingTrackIndex' in play_block
assert 'info.hasStateIndex' in play_block
assert '++spircPlaySelectFrames_' in play_block
assert '++spircPlaySelectByIndex_' in play_block
assert 'makeRetainedSpircState(requestedIndex, 1u, 0u)' in play_block
assert 'sendTrackMetadataRequest(selected)' in play_block

# Next/Prev and Seek preserve the UI playback state independently of key availability.
assert 'mediaKeyServiceBlocked_ ? 2u : spircLastLoadStatus_' not in cpp
assert 'spircLastLoadStatus_ == 1u ? 1u : 2u' in cpp

# Fresh start/reset clears retained queue, command-ack state and virtual playback clock.
start_segment = cpp[cpp.index('bool SpotifySessionProbe::startNow'):cpp.index('bool SpotifySessionProbe::runOneSession')]
assert 'spircStateTrackRefs_.clear()' in start_segment
assert 'spircHaveCommandAck_ = false' in start_segment
assert 'spircPlaybackClockRunning_ = false' in start_segment
assert 'spircPlaySelectFrames_ = 0u' in start_segment
assert 'spircStateTrackRefs_.clear()' not in cpp[cpp.index('bool SpotifySessionProbe::runOneSession'):]

# r5 telemetry exposes selection and playback-clock state.
assert 'playSelect=' in ui and 'byIndex=' in ui
assert 'SPIRC playback status=' in ui and 'clock=' in ui and 'basePos=' in ui
assert 'SPIRC control Notify sent=' in ui
assert 'commandAcks=' in ui and 'recipientIgnored=' in ui
assert 'duplicateLoadAcked=' in ui

assert 'USERMOD_REVISION = "r14"' in manifest

# r7: current Android field-19 payload is binary ContextPlayerState, not historical JSON.
assert 'SPIRC_REPLACE = 0x21u' in cpp
assert 'parseModernContextPlayerState' in cpp
assert 'ContextPlayerState.index = field 6' in cpp
assert 'ContextPlayerState.track = ProvidedTrack field 7' in cpp
assert 'extractLengthDelimited(data, size, 6u, indexMessage)' in cpp
assert 'extractLengthDelimited(data, size, 7u, providedTrack)' in cpp
assert 'extractProtoString(contextTrack.data(), contextTrack.size(), 1u, uri)' in cpp
assert 'info.contextPlayerEncoding == F("proto")' in cpp
assert 'SPIRC contextPlayer frames=' in ui
assert 'encoding=' in ui

# Keep the historical JSON shape only as a compatibility fallback.
assert 'resolveContextPlayerUidToUri' in cpp
assert 'findJsonKeyValueStart(data, size, "skip_to", skipValue)' in cpp
assert 'encoding = F("json")' in cpp

# Capability honesty: this classic receiver must not claim the full playlist-v2 contract.
assert 'addIntCapability(13u, 1)' not in cpp
assert 'kSupportsPlaylistV2 (type 13) is intentionally NOT advertised' in cpp

# Both current-protobuf URI/index and legacy JSON targets resolve against the retained queue.
assert 'resolveContextPlayerSelection' in cpp
assert '++spircContextPlayerSelect_' in cpp
assert 'spircContextPlayerUnresolved' in ui

print("dev.2m-r12 media-key/SPIRC context-player contract: PASS")


# r8: real Android row taps arrive as a second classic Load whose top-level State
# remains on the current track while opaque field 19 changes. Characterize that
# payload safely and correlate exact queue identities before duplicate-Load ACK.
assert 'contextPlayerState.assign(cps, cps + len)' in cpp
assert 'contextPlayerHash = fnv1a32(cps, len)' in cpp
assert 'contextPlayerPrefixHex' in cpp
assert 'contextPlayerMagic' in cpp
assert 'contextPlayerPrintablePct' in cpp
assert 'summarizeContextPlayerProto' in cpp
assert 'scanRetainedQueue' in cpp
assert 'spircContextPlayerQueueMatches_' in cpp
assert 'spircContextPlayerNonCurrentMatches_' in cpp
assert 'nonCurrent == 1u' in cpp
assert '++spircContextPlayerByScan_' in cpp
assert '++spircDuplicateLoadsWithUnknownContext_' in cpp
assert 'SPIRC contextDiag lastBytes=' in ui
assert 'SPIRC contextScan queueMatches=' in ui
assert 'duplicateCpsUnknown=' in ui
assert 'prefix=' in ui and 'magic=' in ui and 'printable=' in ui
assert 'proto=' in ui and 'lenFields=' in ui and 'map=' in ui
assert 'byScan=' in ui

# The opaque payload must never be serialized into /json/info.
assert 'contextPlayerState.data()' not in ui

# r9: r8a hardware proved field 19 is a gzip member. Decode it boundedly using
# ESP32-S3 ROM miniz, verify the gzip CRC/ISIZE, and only then parse/scan.
assert '#include <miniz.h>' in cpp
assert 'SPOTIFY_HAVE_ROM_MINIZ' in cpp
assert 'MAX_CONTEXT_PLAYER_INFLATED_BYTES = 131072u' in cpp
assert 'inflateContextPlayerGzip' in cpp
assert 'tinfl_decompress_mem_to_mem() here' in cpp
assert 'tinfl_decompress_mem_to_mem(' not in cpp.replace('tinfl_decompress_mem_to_mem() here', '')
assert 'heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)' in cpp
assert 'tinfl_decompress(' in cpp
assert 'TINFL_STATUS_DONE' in cpp
assert 'TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF' in cpp
assert 'mz_crc32(MZ_CRC32_INIT' in cpp
assert 'expectedSize > MAX_CONTEXT_PLAYER_INFLATED_BYTES' in cpp
assert 'heap_caps_malloc(expectedSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)' in cpp
assert 'decodedResolvedIndex != trackRefIndex_' in cpp
assert 'outIndex = decodedIndex;' not in cpp
assert '++spircContextPlayerByInflate_' in cpp
assert 'SPIRC contextInflate attempts=' in ui
assert 'byInflate=' in ui
assert 'contextPlayerState.data()' not in ui

print("dev.2m-r12 stack-safe gzip context-player decode contract: PASS")

# r10: r9a hardware proved inflate is stable and yields 100% printable JSON,
# endpoint=play, but UID-only skip_to remained unresolved. Use a whitespace-
# tolerant structural JSON key parser and map track_uid through the enclosing
# context track object instead of exact byte literals.
assert 'findJsonKeyValueStart' in cpp
assert 'jsonCompositeEnd' in cpp
assert 'findEnclosingJsonObject' in cpp
assert 'parseJsonStringAt' in cpp
assert 'findJsonKeyValueStart(data, size, "skip_to", skipValue)' in cpp
assert 'findJsonKeyValueStart(data, size, "uid", valueStart' in cpp
assert 'findEnclosingJsonObject(data, size, keyPos, objectStart, objectEnd)' in cpp
assert 'candidateUri.startsWith(F("spotify:track:"))' in cpp
assert 'std::min(size, skipValue + 2048u)' in cpp

print("dev.2m-r12 structural JSON skip_to/UID mapping contract: PASS")

# r12: r11 hardware resolved track_uid to a Spotify URI, but the retained classic
# queue was GID-only (82 refs / 1476 bytes = 18-byte field-1-only TrackRefs).
# Canonicalize GID-only refs to spotify:track URIs, keep multi-skip convergence,
# and never trust a context track_index without identity validation.
assert 'resolveJsonSkipCandidates' in cpp
assert 'spircContextSkipObjects_ < 16u' in cpp
assert 'extractJsonQuotedValue(payload, payloadSize, "track_uid"' in cpp
assert 'resolveContextPlayerUidToUri(payload, payloadSize, uid, uidUri)' in cpp
assert 'canonicalSpircTrackUri' in cpp
assert 'spotifyTrackUriFromGid(gid.data(), gid.size())' in cpp
assert 'spircContextQueueGidOnly_' in cpp
assert 'spircContextSkipIndexValidated_' in cpp
assert 'spircContextSkipIndexIgnored_' in cpp
assert 'consider(idxValue);' not in cpp
assert 'spircContextSkipAmbiguous_' in cpp
assert 'spircContextSkipUniqueIndex_' in cpp
assert '++spircContextPlayerBySkipScan_' in cpp
assert 'SPIRC contextQueue refs=' in ui
assert 'SPIRC contextSkip objects=' in ui
assert 'indexValidated=' in ui and 'indexIgnored=' in ui
assert 'bySkip=' in ui
assert 'uidUri.c_str()' not in ui
print("dev.2m-r12 canonical GID/URI queue target-resolution contract: PASS")
