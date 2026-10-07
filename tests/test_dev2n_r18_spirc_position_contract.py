#!/usr/bin/env python3
"""Regression guards for dev.2n-r18 SPIRC track-change position semantics."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
s = (root / 'spotify/SpotifySessionProbe.cpp').read_text()
h = (root / 'spotify/SpotifySessionProbe.h').read_text()
u = (root / 'usermod_spotify_connect.h').read_text()
lib = json.loads((root / 'library.json').read_text())

assert lib['version'] == '0.1.0-dev.2n-vorbis-r20'
assert 'USERMOD_REVISION = "r20"' in u

# The observed Android context-player direct selection must never inherit the
# previous track's running position after the resolved queue identity changes.
needle = '''SpircFrameInfo selected = makeRetainedSpircState(contextSelectedIndex,\n                      info.hasPlayStatus && info.playStatus == 2u ? 2u : 1u, 0u);'''
assert needle in s
assert 'sendSpircControlNotify(selected, "reset-track-change")' in s

# Explicit full LOAD changes are reset only after local ownership is already
# established. Initial transfer keeps the controller's current position.
for token in (
    'const bool directTrackChange = spircLocalActive_',
    'loadState.position = 0u;',
    'loadState.positionMs = 0u;',
    'loadState.hasPosition = false;',
    'loadState.hasPositionMs = true;',
    'directTrackChange ? "reset-track-change" : "remote-load"',
):
    assert token in s, token

# All retained-queue navigation that really changes identity uses the same
# semantic source. A NEXT/PREV at the queue boundary must not rewind the same
# track to zero.
assert s.count('"reset-track-change"') >= 6
for token in (
    'const bool trackChanged = nextIndex != trackRefIndex_;',
    'trackChanged ? 0u : currentSpircPositionMs()',
    'trackChanged ? "reset-track-change" : "nav-boundary-current"',
):
    assert token in s, token

# Pause/resume and seek stay current-track operations.
for token in (
    '"play-current"', '"pause-current"', '"playpause-current"', '"seek-explicit"',
    '"retain-empty-load"', '"retain-duplicate-load"', '"retain-replace"',
):
    assert token in s, token

# Blocked-state mirroring must not erase the semantic source of the last user/control
# position operation, so the track-reset evidence remains visible after key failure.
assert 'setSpircPlaybackClock(current.playStatus, current.positionMs, nullptr);' in s

# Runtime evidence must state why the currently advertised base position was set.
for token in (
    'spircPlaybackPositionSource() const',
    'spircPlaybackTrackResets() const',
    'source=', 'trackResets=',
    'scope=dev.2n-r20 r19 encrypted transport frozen',
):
    assert token in h + u, token

# r18 position semantics remain frozen in r19. Audio and every decoder file except
# SpotifyApContinuousRing (which gains r19-only wrap counters) remain byte-identical
# to the r17 baseline; r16 probe and r15 metadata-audit implementations are frozen.
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
    actual = hashlib.sha256((root / rel).read_bytes()).hexdigest()
    assert actual == digest, f'{rel}: {actual} != {digest}'

print('r18 SPIRC position contract PASS inside r19: changed identities reset to zero; current-track controls retain/seek; prior media/key paths frozen except additive r19 ring counters')
