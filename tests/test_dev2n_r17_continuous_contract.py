#!/usr/bin/env python3
"""Static/freeze guards for dev.2n-r17 continuous encrypted AP transport."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
s = (root/'spotify/SpotifySessionProbe.cpp').read_text()
h = (root/'spotify/SpotifySessionProbe.h').read_text()
u = (root/'usermod_spotify_connect.h').read_text()
rh = (root/'decoder/SpotifyApContinuousRing.h').read_text()
rc = (root/'decoder/SpotifyApContinuousRing.cpp').read_text()

freeze = json.loads((root/'tests/fixtures/r16_frozen_paths.json').read_text())
for path, digest in freeze['files'].items():
    assert hashlib.sha256((root/path).read_bytes()).hexdigest() == digest, path
for name, item in freeze['blocks'].items():
    a = s.index(item['start'])
    b = s.index(item['end'], a + len(item['start']))
    assert hashlib.sha256(s[a:b].encode()).hexdigest() == item['sha256'], name

for token in (
    'AP_CONTINUOUS_RANGE_BYTES = 4096u',
    'AP_CONTINUOUS_RANGE_COUNT = 16u',
    'AP_CONTINUOUS_TARGET_BYTES',
    'SpotifyApContinuousRing apContinuousRing_',
    'apContinuousTrackChangeCancels_',
):
    assert token in h, token

for token in (
    'kCapacityBytes = 64u * 1024u',
    'bool push(uint32_t absoluteOffset',
    'backpressureEvents()',
    'gapErrors()',
    'duplicateErrors()',
    'MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT',
):
    assert token in rh + rc, token

# The new transport remains encrypted-byte only.
for forbidden in ('SpotifyAudioAesCtr', 'SpotifyVorbisFixturePlayer', 'enqueuePcm44100', 'audioKey_', 'AUDIO_AES_KEY_BYTES'):
    assert forbidden not in rh + rc, forbidden

# It starts only after the frozen 3x4096 canary source contract + verification.
assert 'apStreamSourceContractReady() && apStreamLiveVerifySuccesses_ != 0u' in s
assert 'buildApStreamChunkRequest(' in s
assert 'apContinuousConsumerHash_ = fnv1a32Update' in s
assert 'apContinuousProducerHash_ = fnv1a32Update' in s
assert 'apContinuousRing_.push(absoluteOffset' in s
assert 'drainApContinuousRing()' in s
assert 'apContinuousRing_.finishProducer()' in s
assert 'apContinuousProducerHash_ == apContinuousConsumerHash_' in s
assert 'apContinuousRing_.gapErrors() == 0u' in s
assert 'apContinuousRing_.duplicateErrors() == 0u' in s
assert 'clearApContinuousTrackState();' in s

# r17 must not open the live decryption/decoder gate.
continuous = s[s.index('// dev.2n-r17: secondary continuous encrypted transport diagnostic.'):s.index('    if (liveCommand == AES_KEY_COMMAND || liveCommand == AES_KEY_ERROR_COMMAND)')]
for forbidden in ('SpotifyAudioAesCtr', 'SpotifyVorbisFixturePlayer', 'enqueuePcm44100', 'memcpy(audioKey_', 'AES_KEY_COMMAND)'):
    assert forbidden not in continuous, forbidden

for token in ('AP Continuous state=', 'AP Continuous ring storage=', 'AP Continuous integrity ',
              'consumer=diagnostic decrypt=closed',
              'scope=dev.2n-r20 r19 encrypted transport frozen'):
    assert token in u, token

lib = json.loads((root/'library.json').read_text())
assert lib['version'] == '0.1.0-dev.2n-vorbis-r20'
assert 'USERMOD_REVISION = "r20"' in u
print('r17 continuous transport/freeze PASS inside r19: r16 key/audit files + normal key/canary blocks frozen; 64 KiB stage remains decrypt/decoder closed')
