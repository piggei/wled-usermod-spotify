#!/usr/bin/env python3
"""Static/freeze guards for dev.2n-r19 extended encrypted AP transport."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
s = (root/'spotify/SpotifySessionProbe.cpp').read_text()
h = (root/'spotify/SpotifySessionProbe.h').read_text()
u = (root/'usermod_spotify_connect.h').read_text()
rh = (root/'decoder/SpotifyApContinuousRing.h').read_text()
rc = (root/'decoder/SpotifyApContinuousRing.cpp').read_text()
lib = json.loads((root/'library.json').read_text())

assert lib['version'] == '0.1.0-dev.2n-vorbis-r20'
assert 'USERMOD_REVISION = "r20"' in u

# r17 baseline remains unchanged in geometry and is the explicit prerequisite.
for token in (
    'AP_CONTINUOUS_RANGE_BYTES = 4096u',
    'AP_CONTINUOUS_RANGE_COUNT = 16u',
    'AP_CONTINUOUS_TARGET_BYTES',
    'if (!apExtendedAttemptedForTrack_ && apContinuousComplete_ && !apContinuousPending_)',
):
    assert token in h + s, token

# r19 sustained geometry: 1 MiB = 256 exact 4096-byte ranges, starting after
# the already-qualified first 64 KiB rather than replacing the r17 stage.
for token in (
    'AP_EXTENDED_RANGE_BYTES = 4096u',
    'AP_EXTENDED_SUSTAINED_START_BYTES = AP_CONTINUOUS_TARGET_BYTES',
    'AP_EXTENDED_SUSTAINED_RANGE_COUNT = 256u',
    'AP_EXTENDED_SUSTAINED_TARGET_BYTES',
    'sendApExtendedSustainedRange',
    'apExtendedSustainedCompletedRanges_ == AP_EXTENDED_SUSTAINED_RANGE_COUNT',
):
    assert token in h + s, token

# Exact tail must derive from AP-reported file size and end exactly at that byte.
for token in (
    '((apExtendedReportedFileBytes_ - 1u) / AP_EXTENDED_RANGE_BYTES)',
    'apExtendedReportedFileBytes_ - apExtendedTailStartBytes_',
    'apExtendedTailStartBytes_ + apExtendedTailDataBytes_ ==',
    'apExtendedReportedFileBytes_',
    'apExtendedTailExactBoundary_',
):
    assert token in h + s, token

# Producer/consumer integrity and bounded storage stay closed to decrypt/decoder.
for token in (
    'SpotifyApContinuousRing apExtendedRing_',
    'apExtendedRing_.begin(AP_EXTENDED_SUSTAINED_START_BYTES',
    'apExtendedRing_.push(absoluteOffset',
    'drainApExtendedRing()',
    'apExtendedRing_.finishProducer()',
    'apExtendedSustainedProducerHash_ == apExtendedSustainedConsumerHash_',
    'apExtendedTailProducerHash_ == apExtendedTailConsumerHash_',
    'writeWraps()', 'readWraps()',
):
    assert token in h + s + rh + rc, token

# Track-change cancellation records the in-flight stage/amount and invalidates
# the ring before per-track state is cleared, ensuring unread encrypted bytes are wiped.
for token in (
    '++apExtendedTrackChangeCancels_',
    'apExtendedLastCancelProducedBytes_',
    'apExtendedLastCancelBufferedBytes_',
    'apExtendedRing_.invalidate();',
    'clearApExtendedTrackState();',
):
    assert token in s, token

# Channel ownership is isolated from the frozen r17 handler.
for token in (
    'apExtendedFirstChannelId_',
    'apExtendedAllocatedChannels_',
    'const uint16_t delta = static_cast<uint16_t>(extendedChannelId - apExtendedFirstChannelId_)',
    'delta < apExtendedAllocatedChannels_',
):
    assert token in h + s, token

# The r19 code sections must remain encrypted-byte diagnostic only.
a = s.index('// dev.2n-r19: after the frozen r17 64 KiB stage has passed')
b = s.index('    if (WiFi.status() != WL_CONNECTED)', a)
start_block = s[a:b]
a2 = s.index('// r19 extended transport owns only the contiguous channel-id span allocated')
b2 = s.index('// r17 continuous transport is a separate phase/channel namespace layered', a2)
response_block = s[a2:b2]
for forbidden in ('SpotifyAudioAesCtr', 'SpotifyVorbisFixturePlayer', 'enqueuePcm44100', 'memcpy(audioKey_', 'AUDIO_AES_KEY_BYTES'):
    assert forbidden not in start_block + response_block, forbidden

for token in (
    'AP Extended state=',
    'AP Extended sustained start=',
    'AP Extended sustainedRing storage=',
    'AP Extended tail start=',
    'AP Extended cancel lastStage=',
    'scope=dev.2n-r20 r19 encrypted transport frozen',
    'consumer=diagnostic decrypt=closed',
):
    assert token in u, token

# Audio sink, decoder, metadata audit and manual key-probe implementations stay
# frozen. The only decoder file intentionally changed is the ring's additive
# wrap telemetry; SessionProbe/UI/version/docs/tests are the integration surface.
frozen = {
'audio/ES8311Codec.cpp':'1e1bd5cbfb82f83532ff26900b4982b798deaec75c9e43057cf44daf5c7ff333',
'audio/ES8311Codec.h':'ff3a4619bdd7a52d907c64670bc050d09a3c043227ec6a0fe1721cb036cf1a4f',
'audio/SpotifyAudioConfig.h':'2ad48aa43adc99674f6f91abd808fe1a5d2a20be47d3e8e9c777d9fcd0200a06',
'audio/WavesharePcmOutput.cpp':'c8680bb35cc8293153af0d1f8621356500b19f1bc7299ec1c5cf1f0d3feb6aa0',
'audio/WavesharePcmOutput.h':'d2b05868c657aa83b0fa1fc65c1c6844d598c4c3f6784e321fad7c94b769e3cf',
'decoder/SpotifyApMediaChunkSource.cpp':'4c5d8df93b1feb2d261e2e2bbd173aff873236cff5ba7f507ce4ae82b1bbb347',
'decoder/SpotifyApMediaChunkSource.h':'8146bdd539a5c92ef7517e00293f3a1b3739559d320f36f50a11f6522fa16d1d',
'decoder/SpotifyAudioAesCtr.cpp':'1cf7b3580a466ec5731ffcfee54b58b187318301e70ae288905c1b63229cc86f',
'decoder/SpotifyAudioAesCtr.h':'52490353f2e70bf82a14259d6db741174645682a878aafc906de160e1c87a31c',
'decoder/SpotifyMediaChunkSource.h':'1da6624a1d46a11468eca3c2ab0abda30e772be5644d2215d3239d34a8221353',
'decoder/SpotifyVorbisEncryptedFixture.h':'aaed7b647423978a8268a16fb3bfb3a49ae0aa7b92955398a77f5b81d8be995a',
'decoder/SpotifyVorbisFixture.h':'44f83265f8ce3c277600753fe7f6b1d64fad5faa6f293f54219103429c7ee7d2',
'decoder/SpotifyVorbisFixturePlayer.cpp':'430bc9e72d043f66dff19bee6f48187b4eb0c6bf85bc5bccafd40e2ce940433f',
'decoder/SpotifyVorbisFixturePlayer.h':'2127c6c4d260ac1ba94ff0618169e680f962da0c54fd0f63051f359bd2f4f2cf',
'spotify/SpotifyAudioKeyProbe.cpp':'cfeaf5e0b03661d64acf3dde22a747763f75dd0e8d85bf6027ee638498bd42f6',
'spotify/SpotifyAudioKeyProbe.h':'035f6c711b8e253d029730f766faa3b9cc6d8e6dd05dc12dbb16b673ac6795ee',
'spotify/SpotifySessionKeyProbe.cpp':'003e5312f6a9378f1460bc88d2af95a2ae8fe71c76219830f25a2962771d47ae',
'spotify/SpotifyMetadataAudit.cpp':'83515a76db830901df44fd4745acd86d557aa8019ca511d91df122ab3fb6735c',
'spotify/SpotifyMetadataAudit.h':'8259bba4b5d0292a5a5e124a47b4fecb79b87e7551c126c3da98599542a1c458',
}
for rel, digest in frozen.items():
    actual = hashlib.sha256((root/rel).read_bytes()).hexdigest()
    assert actual == digest, f'{rel}: {actual} != {digest}'

print('r19 extended transport contract PASS: frozen r17+r18 gates, 1 MiB bounded rolling path, exact EOF tail, cancellation wipe, decrypt closed')
