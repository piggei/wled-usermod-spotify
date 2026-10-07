#!/usr/bin/env python3
from pathlib import Path
import hashlib, re

ROOT = Path(__file__).resolve().parents[1]
plain_h = (ROOT / 'decoder/SpotifyVorbisFixture.h').read_text()
enc_h = (ROOT / 'decoder/SpotifyVorbisEncryptedFixture.h').read_text()
aes_cpp = (ROOT / 'decoder/SpotifyAudioAesCtr.cpp').read_text()
player = (ROOT / 'decoder/SpotifyVorbisFixturePlayer.cpp').read_text()
ui = (ROOT / 'usermod_spotify_connect.h').read_text()


def extract_data(text: str) -> bytes:
    m = re.search(r'DATA\[\].*?=\s*\{(.*?)\};', text, re.S)
    assert m, 'DATA array missing'
    return bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', m.group(1)))

plain = extract_data(plain_h)
enc = extract_data(enc_h)
assert len(plain) == len(enc) == 10437
assert hashlib.sha256(plain).hexdigest() == '19c5cbe4a2636a44b6c18632bdf8958aa61a9713dfb1a8bb0829c9ce93eb6e1c'
assert hashlib.sha256(enc).hexdigest() == 'da4393cc6c6fc2a002d2c2b7c1452b0c964928d44291c5cb99b133fb214704fb'
assert plain[:16].hex() == '4f67675300020000000000000000350a'
assert enc[:16].hex() == 'ba2f620f9e4254597904a7d188c4b073'
assert bytes(a ^ b for a, b in zip(plain[:16], enc[:16])).hex() == 'f548055c9e4054597904a7d188c48579'

# Legacy Spotify media decryptor contract mirrored from current librespot:
# AES-128 CTR and the fixed 16-byte audio IV.  The fixture key is intentionally
# synthetic and public; this test never contains a real Spotify AudioKey.
for b in ('0x72', '0xe0', '0x67', '0xfb', '0xdd', '0xcb', '0xcf', '0x77',
          '0xeb', '0xe8', '0xbc', '0x64', '0x3f', '0x63', '0x0d', '0x93'):
    assert b in aes_cpp
assert 'mbedtls_aes_setkey_enc(&aes_, key, 128u)' in aes_cpp
assert 'mbedtls_aes_crypt_ctr' in aes_cpp
assert 'SpotifyVorbisEncryptedFixture::KEY' in player
assert 'decryptor->transform' in player
assert 'INPUT_CHUNK_BYTES = 4096u' in player
assert 'startAesChunked' in player
assert 'action == "start-vorbis-aes-chunked"' in ui
assert 'vorbis crypto mode=' in ui
assert 'decryptBytes=' in ui
assert 'complete-aes-chunked-input-exhausted' in player
print('dev.2n-r9 AES-CTR chunked pipeline contract PASS')
