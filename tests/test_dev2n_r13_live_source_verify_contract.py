#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source_h = (ROOT / 'decoder/SpotifyApMediaChunkSource.h').read_text()
source_cpp = (ROOT / 'decoder/SpotifyApMediaChunkSource.cpp').read_text()
session_h = (ROOT / 'spotify/SpotifySessionProbe.h').read_text()
session_cpp = (ROOT / 'spotify/SpotifySessionProbe.cpp').read_text()
ui = (ROOT / 'usermod_spotify_connect.h').read_text()

# Consumer-side cursor can be exercised without wiping the encrypted capture.
assert 'void rewindRead();' in source_h
assert 'void SpotifyApMediaChunkSource::rewindRead()' in source_cpp
assert 'readOffset_ = 0u;' in source_cpp
assert 'readChunks_ = 0u;' in source_cpp

# r13 verifier must consume only through the MediaChunkSource contract, using
# a small bounded stack scratch buffer, compare against the independent AP
# sourceGate, and rewind after verification.
assert 'verifyApStreamMediaSource' in session_cpp
assert 'uint8_t scratch[512];' in session_cpp
assert 'apStreamMediaSource_.next(scratch, sizeof(scratch), written)' in session_cpp
assert 'fnv1a32Update(apStreamLiveVerifyHash_, scratch, written)' in session_cpp
assert 'apStreamLiveVerifyHash_ == apStreamCipherHash_' in session_cpp
assert 'apStreamLiveVerifyBytes_ == apStreamCipherBytesHashed_' in session_cpp
assert 'apStreamLiveVerifyChunks_ == apStreamSourceChunks_' in session_cpp
assert session_cpp.count('apStreamMediaSource_.rewindRead();') >= 2
assert 'apStreamLiveVerifyReadCalls_ > 64u' in session_cpp

# The live verification remains diagnostic-only: no live AP source is wired to
# AES/Vorbis in SpotifySessionProbe.
assert 'SpotifyAudioAesCtr' not in session_cpp
assert 'SpotifyVorbisFixturePlayer' not in session_cpp
assert 'vorbisFixture_.start' not in session_cpp

# Telemetry exposes only bounded counters/digest equality, never media bytes.
assert 'AP Stream liveVerify attempts=' in ui
assert 'hashMatch=' in ui and 'rewind=' in ui and 'decrypt=closed' in ui
assert 'consumer=closed keyGate=' in ui
assert 'hard key gate + live decrypt/decoder consumer remain closed' in ui
assert 'USERMOD_REVISION = "r17"' in ui

# Public getters exist for the verification result.
for token in [
    'apStreamLiveVerifyAttempts()', 'apStreamLiveVerifySuccesses()',
    'apStreamLiveVerifyFailures()', 'apStreamLiveVerifyReadCalls()',
    'apStreamLiveVerifyChunks()', 'apStreamLiveVerifyBytes()',
    'apStreamLiveVerifyHash()', 'apStreamLiveVerifyHashMatch()',
    'apStreamLiveVerifyEof()', 'apStreamLiveVerifyRewound()'
]:
    assert token in session_h

print('dev.2n-r13 live MediaChunkSource diagnostic-consumer verification contract PASS')
