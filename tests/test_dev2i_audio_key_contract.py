#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp = (root / "spotify/SpotifySessionProbe.cpp").read_text()
h = (root / "spotify/SpotifySessionProbe.h").read_text()
ui = (root / "usermod_spotify_connect.h").read_text()

# dev.2i-r1 wire contract remains unchanged.
assert "REQUEST_KEY_COMMAND = 0x0Cu" in cpp
assert "AES_KEY_COMMAND = 0x0Du" in cpp
assert "AES_KEY_ERROR_COMMAND = 0x0Eu" in cpp
assert "buildAudioKeyRequest" in cpp
assert "AUDIO_FILE_ID_BYTES + TRACK_GID_BYTES + 6u" in cpp
assert "out.insert(out.end(), fileId, fileId + AUDIO_FILE_ID_BYTES)" in cpp
assert "out.insert(out.end(), trackGid, trackGid + TRACK_GID_BYTES)" in cpp
assert "audioKeyNextSequence_++" in cpp
assert "sendShannonPacket(tcp, sendCipher, sendNonce, REQUEST_KEY_COMMAND" in cpp
assert "readBe32Prefix(payload, responseSequence)" in cpp
assert "payload.size() != 4u + AUDIO_AES_KEY_BYTES" in cpp
assert "memcpy(audioKey_, payload.data() + 4u, AUDIO_AES_KEY_BYTES)" in cpp
assert "AUDIO_KEY_TIMEOUT_MS = 2500u" in h

# r2 keeps all valid metadata AudioFile ids in a bounded diagnostic candidate set.
assert "MAX_AUDIO_KEY_CANDIDATES = 8u" in h
assert "std::vector<AudioFileCandidate> audioFiles" in cpp
assert "info.audioFiles.push_back(candidate)" in cpp
assert "audioKeyCandidateFileIds_[MAX_AUDIO_KEY_CANDIDATES][20]" in h
assert "addAudioKeyCandidate" in cpp
assert "sendAudioKeyCandidate" in cpp
assert "advanceAudioKeyCandidate" in cpp
assert "audioKeyCandidateAdvances_" in cpp
assert "audioKeyCandidateTruncated_" in cpp
assert "audioKeyServiceRejects_" in cpp
assert "audioKeyProtocolErrors_" in cpp

# Track changes cancel the old in-flight request; late sequence responses are stale, not accepted.
assert "memcmp(selectedTrackGid_, remote.selectedTrackGid.data(), TRACK_GID_BYTES)" in cpp
assert "++audioKeyTrackChangeCancels_" in cpp
assert "++audioKeyStaleResponses_" in cpp
assert "audioKeyPendingSequence_ = 0u" in cpp

# Timeouts and AesKeyError advance only within the bounded candidate set.
assert "audioKeyCandidateTimedOut_[timedOutCandidate] = true" in cpp
assert '"Spotify AesKeyError; trying next audio file"' in cpp
assert '"Spotify AesKeyError for all audio files"' in cpp

# Diagnostics expose only counters/formats/results, never the 16-byte AES key.
assert "AudioKey requests=" in ui
assert "rejects=" in ui and "protoErr=" in ui and "stale=" in ui and "trackCancel=" in ui
assert "AudioKey candidate=" in ui
assert "AudioKey candidates" in ui
assert "keyBytes=" in ui
assert "audioKeyHex" not in ui
assert 'USERMOD_VERSION = "0.1.0-dev.2l-ap-stream"' in ui
assert 'USERMOD_REVISION = "r1"' in ui
assert "ProductInfo headFiles=0 -> AP StreamChunk 4KiB encrypted canary" in ui
print("dev.2i-r2 audio-key candidate diagnostics contract: PASS")
