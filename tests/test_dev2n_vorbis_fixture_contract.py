#!/usr/bin/env python3
from pathlib import Path
import json, re, struct

root = Path(__file__).resolve().parents[1]
ui = (root / 'usermod_spotify_connect.h').read_text()
player_h = (root / 'decoder' / 'SpotifyVorbisFixturePlayer.h').read_text()
player_cpp = (root / 'decoder' / 'SpotifyVorbisFixturePlayer.cpp').read_text()
fixture_h = (root / 'decoder' / 'SpotifyVorbisFixture.h').read_text()
lib = json.loads((root / 'library.json').read_text())

assert 'USERMOD_VERSION = "0.1.0-dev.2n-vorbis"' in ui
assert 'USERMOD_REVISION = "r17"' in ui
assert 'action == "start-vorbis"' in ui
assert 'vorbisFixture=' in ui
assert 'vorbis PCM ' in ui
assert 'vorbis timing decodeMax=' in ui
assert 'vorbis memory internal=' in ui

assert '+<decoder/*.cpp>' in lib['build']['srcFilter']
assert lib['dependencies'] == [{'owner': 'esphome', 'name': 'micro-vorbis', 'version': '^0.1.0', 'platforms': 'espressif32'}]
assert '#include <micro_vorbis/ogg_vorbis_decoder.h>' in player_cpp
assert 'OggVorbisDecoder(2u, false)' in player_cpp
assert 'enqueuePcm44100' in player_cpp
assert 'MALLOC_CAP_SPIRAM' in player_cpp
assert 'TASK_STACK_BYTES = 12288u' in player_cpp
assert 'OUTPUT_BUFFER_BYTES = 16u * 1024u' in player_cpp
assert 'unexpected-pcm-format' in player_cpp
assert 'pcm-feed-failed' in player_cpp
assert 'decoder-no-progress' in player_cpp
assert 'input-exhausted-frame-mismatch' in player_cpp
assert 'OGG_VORBIS_DECODER_END_OF_STREAM' in player_cpp
assert 'SpotifyVorbisFixturePlayer' in player_h

# Reconstruct the generated fixture bytes from the C header and validate the
# Ogg/Vorbis identification header without needing ffmpeg or a host codec.
body = fixture_h.split('static const uint8_t DATA[] PROGMEM = {', 1)[1].split('};', 1)[0]
blob = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', body))
size_match = re.search(r'static constexpr size_t SIZE = (\d+)u;', fixture_h)
assert size_match and int(size_match.group(1)) == len(blob)
assert 8_000 <= len(blob) <= 20_000
assert blob[:4] == b'OggS'
ident = blob.find(b'\x01vorbis')
assert ident >= 0
assert struct.unpack_from('<I', blob, ident + 7)[0] == 0  # Vorbis version
assert blob[ident + 11] == 2                              # stereo
assert struct.unpack_from('<I', blob, ident + 12)[0] == 44100
assert b'\x03vorbis' in blob and b'\x05vorbis' in blob

# Walk Ogg pages far enough to prove the checked-in fixture is structurally
# bounded and terminated with an EOS page.
off = 0
pages = 0
saw_eos = False
while off < len(blob):
    assert blob[off:off+4] == b'OggS'
    assert off + 27 <= len(blob)
    header_type = blob[off + 5]
    segs = blob[off + 26]
    assert off + 27 + segs <= len(blob)
    body_len = sum(blob[off + 27:off + 27 + segs])
    page_len = 27 + segs + body_len
    assert page_len > 27 and off + page_len <= len(blob)
    pages += 1
    saw_eos |= bool(header_type & 0x04)
    off += page_len
assert off == len(blob)
assert pages >= 3 and saw_eos

assert (root / 'docs' / 'DEV2N_LOCAL_VORBIS.md').exists()
assert 'micro-vorbis local decoder dependency' in (root / 'THIRD_PARTY_NOTICES.md').read_text()

print(f'dev.2n-r3 local Ogg/Vorbis fixture contract: PASS ({len(blob)} bytes, {pages} Ogg pages)')
