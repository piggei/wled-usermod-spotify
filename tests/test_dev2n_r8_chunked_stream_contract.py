#!/usr/bin/env python3
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
h = (root / 'decoder/SpotifyVorbisFixturePlayer.h').read_text()
cpp = (root / 'decoder/SpotifyVorbisFixturePlayer.cpp').read_text()
ui = (root / 'usermod_spotify_connect.h').read_text()
fixture_h = (root / 'decoder/SpotifyVorbisFixture.h').read_text()

assert 'USERMOD_REVISION = "r17"' in ui
assert 'action == "start-vorbis-chunked"' in ui
assert 'startChunked(audio_)' in ui
assert 'vorbis stream mode=' in ui
assert 'INPUT_CHUNK_BYTES = 4096u' in cpp
assert 'INPUT_STAGING_BYTES = 8192u' in cpp
assert 'bool SpotifyVorbisFixturePlayer::startChunked' in cpp
assert 'memmove(staging, staging + consumed, left)' in cpp
assert 'telemetry_.sourceSupplied' in cpp
assert 'telemetry_.chunkLoads' in cpp
assert 'telemetry_.stagingHighWater' in cpp
assert 'telemetry_.refillWaits' in cpp
assert 'complete-chunked-input-exhausted' in cpp
assert 'complete-chunked-eos' in cpp
assert 'premature-eos' in cpp
assert 'uint32_t chunkLoads = 0;' in h
assert 'InputMode inputMode = InputMode::Contiguous;' in h

body = fixture_h.split('static const uint8_t DATA[] PROGMEM = {', 1)[1].split('};', 1)[0]
blob = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', body))
assert len(blob) == 10437
chunks = [blob[i:i+4096] for i in range(0, len(blob), 4096)]
assert [len(x) for x in chunks] == [4096, 4096, 2245]
assert b''.join(chunks) == blob

# Prove the fixture actually exercises transport boundaries rather than merely
# being split between complete Ogg pages. Two Ogg pages cross a 4096-byte edge.
off = 0
crossing_pages = 0
while off < len(blob):
    assert blob[off:off+4] == b'OggS'
    segs = blob[off + 26]
    body_len = sum(blob[off + 27:off + 27 + segs])
    page_len = 27 + segs + body_len
    if off // 4096 != (off + page_len - 1) // 4096:
        crossing_pages += 1
    off += page_len
assert off == len(blob)
assert crossing_pages >= 2

print(f'dev.2n-r8 chunked stream contract PASS (chunks=4096,4096,2245 crossing_pages={crossing_pages})')
