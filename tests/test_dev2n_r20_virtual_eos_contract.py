#!/usr/bin/env python3
"""Static/freeze guards for dev.2n-r20 virtual EOS auto-advance integration."""
import hashlib, json
from pathlib import Path
root = Path(__file__).resolve().parents[1]
s = (root/'spotify/SpotifySessionProbe.cpp').read_text()
h = (root/'spotify/SpotifySessionProbe.h').read_text()
u = (root/'usermod_spotify_connect.h').read_text()
policy = (root/'spotify/SpotifySpircEosPolicy.h').read_text()
lib = json.loads((root/'library.json').read_text())

assert lib['version'] == '0.1.0-dev.2n-vorbis-r20'
assert 'USERMOD_REVISION = "r20"' in u

# Policy is intentionally pure/side-effect free.
for token in ('Action::Advance', 'Action::HoldRepeat', 'Action::HoldBoundary',
              'generation == in.handledGeneration', 'positionMs < in.durationMs'):
    assert token in policy, token
for forbidden in ('WiFiClient', 'sendShannonPacket', 'SpotifyAudioAesCtr', 'enqueuePcm44100'):
    assert forbidden not in policy, forbidden

# Duration is only valid for EOS when it belongs to the current metadata generation.
assert 'metadataPlaybackGeneration_ = metadataAuditGeneration_;' in s
assert 'metadataPlaybackGeneration_ == metadataAuditGeneration_' in s
assert 'spircEosHandledGeneration_' in s

# Network commands win races: EOS is evaluated only while the AP socket is idle.
eos_anchor = s.index('// dev.2n-r20: virtual EOS is a temporary player-lifecycle source')
assert 'if (tcp.available() <= 0 && metadataPlaybackGeneration_ == metadataAuditGeneration_)' in s[eos_anchor:eos_anchor+5000]

# Automatic next is position-zero, retained-queue based, sends Notify, then uses the normal metadata path.
for token in (
    'const uint32_t nextIndex = trackRefIndex_ + 1u;',
    'SpircFrameInfo nextState = makeRetainedSpircState(nextIndex, 1u, 0u);',
    'sendSpircControlNotify(nextState, "auto-next-eos")',
    'sendTrackMetadataRequest(nextState)',
    '++spircAutoAdvanceSuccesses_;',
): assert token in s, token

# One-shot and conservative guards.
for token in (
    'spircEosHandledGeneration_ = metadataPlaybackGeneration_;',
    'spircStateHasRepeat_ && spircStateRepeat_',
    '++spircAutoAdvanceRepeatHolds_',
    '++spircAutoAdvanceBoundaryHolds_',
): assert token in s, token

# Explicit runtime evidence.
for token in (
    'SPIRC EOS source=virtual-clock events=',
    'SPIRC autoAdvance attempts=',
    'lastAction=',
    'auto-next-eos',
    'scope=dev.2n-r20 r19 encrypted transport frozen',
): assert token in u+s, token

# r19 extended transport core blocks remain byte-identical to the hardware-qualified baseline.
blocks = [
  ('// dev.2n-r19: after the frozen r17 64 KiB stage has passed', '    if (WiFi.status() != WL_CONNECTED)', 'd2dda44ade6f8f24a4ca20eb1dd4e8d18793a0a6a3ff7c92b38506abefa1794c'),
  ('// r19 extended transport owns only the contiguous channel-id span allocated', '// r17 continuous transport is a separate phase/channel namespace layered', 'd5033089fd731a91c6f590f304b1fdc95d5c37fda1ca2db4b81eb6ef12969ae5'),
]
for a,b,digest in blocks:
    i=s.index(a); j=s.index(b,i)
    actual=hashlib.sha256(s[i:j].encode()).hexdigest()
    assert actual==digest, (a, actual, digest)

# Frozen media/key implementations stay byte-identical to r19.
frozen = {
'audio/ES8311Codec.cpp':'1e1bd5cbfb82f83532ff26900b4982b798deaec75c9e43057cf44daf5c7ff333',
'audio/ES8311Codec.h':'ff3a4619bdd7a52d907c64670bc050d09a3c043227ec6a0fe1721cb036cf1a4f',
'audio/SpotifyAudioConfig.h':'2ad48aa43adc99674f6f91abd808fe1a5d2a20be47d3e8e9c777d9fcd0200a06',
'audio/WavesharePcmOutput.cpp':'c8680bb35cc8293153af0d1f8621356500b19f1bc7299ec1c5cf1f0d3feb6aa0',
'audio/WavesharePcmOutput.h':'d2b05868c657aa83b0fa1fc65c1c6844d598c4c3f6784e321fad7c94b769e3cf',
'decoder/SpotifyApContinuousRing.cpp':'9928af7ca8ca2a383403ee27555789d4b8a731384f3e8366d76b2ee5b0703ab6',
'decoder/SpotifyApContinuousRing.h':'762b334e4ce2a50829d8e3b40f91d26aafbbf18e845b9d75613a8b5f4cc1ca0f',
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
for rel,digest in frozen.items():
    actual=hashlib.sha256((root/rel).read_bytes()).hexdigest()
    assert actual==digest, f'{rel}: {actual} != {digest}'

print('r20 virtual EOS contract PASS: idle-socket one-shot auto-next, metadata-generation bound; r19 transport and media/key paths frozen')
