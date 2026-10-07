#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source_h = (ROOT / 'decoder/SpotifyMediaChunkSource.h').read_text()
player = (ROOT / 'decoder/SpotifyVorbisFixturePlayer.cpp').read_text()
player_h = (ROOT / 'decoder/SpotifyVorbisFixturePlayer.h').read_text()
session = (ROOT / 'spotify/SpotifySessionProbe.cpp').read_text()
session_h = (ROOT / 'spotify/SpotifySessionProbe.h').read_text()
ui = (ROOT / 'usermod_spotify_connect.h').read_text()

assert 'class SpotifyMediaChunkSource' in source_h
assert 'virtual bool next(uint8_t* dst, size_t capacity, size_t& written) = 0;' in source_h
assert 'class SpotifyMemoryChunkSource final' in source_h
assert 'SpotifyMemoryChunkSource plainSource' in player
assert 'SpotifyMemoryChunkSource encryptedSource' in player
assert 'SpotifyMediaChunkSource* chunkSource' in player
assert 'chunkSource->next' in player
assert 'chunkSource->eof()' in player
assert 'sourceName() const' in player_h

# The r11 observation gate remains as a regression diagnostic even after r12
# adds a separate transient encrypted source. Hash/shape telemetry must still be
# generated directly from the received AP packet bytes.
assert 'apStreamCipherHash_ = fnv1a32Update' in session
assert 'apStreamCipherBytesHashed_ += packetDataBytes' in session
assert 'apStreamSourceChunks_' in session
assert 'apStreamSourceContractReady() const' in session_h
assert 'AP Stream sourceGate chunks=' in ui
assert 'telemetryStorage=none' in ui
print('dev.2n-r11 MediaChunkSource/source-gate regression contract PASS')
