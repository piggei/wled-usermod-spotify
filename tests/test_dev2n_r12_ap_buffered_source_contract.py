#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
h = (ROOT / 'decoder/SpotifyApMediaChunkSource.h').read_text()
cpp = (ROOT / 'decoder/SpotifyApMediaChunkSource.cpp').read_text()
session_h = (ROOT / 'spotify/SpotifySessionProbe.h').read_text()
session = (ROOT / 'spotify/SpotifySessionProbe.cpp').read_text()
ui = (ROOT / 'usermod_spotify_connect.h').read_text()

assert 'class SpotifyApMediaChunkSource final : public SpotifyMediaChunkSource' in h
assert 'kChunkBytes = 4096u' in h
assert 'kChunkCount = 3u' in h
assert 'kCapacityBytes = kChunkBytes * kChunkCount' in h
assert 'appendFragment' in h and 'finishChunk' in h and 'invalidate' in h
assert 'heap_caps_malloc(kCapacityBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)' in cpp
assert 'memset(storage_, 0, kCapacityBytes)' in cpp
assert 'ready() && readOffset_ >= writeOffset_' in cpp

assert '#include "../decoder/SpotifyApMediaChunkSource.h"' in session_h
assert 'SpotifyApMediaChunkSource apStreamMediaSource_' in session_h
assert 'apStreamMediaSource_.appendFragment(payload.data() + offset, packetDataBytes)' in session
assert 'apStreamMediaSource_.finishChunk()' in session
assert 'apStreamMediaSource_.reset()' in session
assert 'apStreamMediaSource_.invalidate()' in session

# Hard fence: r12 buffers only encrypted canary bytes. No live AP source is
# handed to the local fixture player and no audio key is exported to it.
assert 'vorbisFixture_.start' not in session
assert 'SpotifyAudioAesCtr' not in session
assert 'audioKey_' not in cpp
assert 'consumer=closed keyGate=' in ui
assert 'AP Stream liveSource source=ap-encrypted-canary contract=MediaChunkSource ready=' in ui
assert 'hard key gate + live decrypt/decoder consumer remain closed' in ui
assert 'USERMOD_REVISION = "r17"' in ui
print('dev.2n-r12 transient AP MediaChunkSource/hard-key-gate contract PASS')
