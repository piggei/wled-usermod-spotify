#!/usr/bin/env python3
from pathlib import Path
import re
root = Path(__file__).resolve().parents[1]
h = (root / 'decoder/SpotifyVorbisFixturePlayer.h').read_text()
cpp = (root / 'decoder/SpotifyVorbisFixturePlayer.cpp').read_text()
ui = (root / 'usermod_spotify_connect.h').read_text()
fixture = (root / 'decoder/SpotifyVorbisFixture.h').read_text()
assert 'uint32_t eosReports = 0;' in h
assert 'uint32_t eofCompletions = 0;' in h
assert 'uint32_t expectedFrames() const;' in h
assert 'complete-input-exhausted' in cpp
assert 'input-exhausted-frame-mismatch' in cpp
assert 'telemetry_.inputConsumed == SpotifyVorbisFixture::SIZE' in cpp
assert 'telemetry_.pcmFrames == expectedFrames()' in cpp
assert 'telemetry_.eosReports' in cpp
assert 'F(" eos=")' in ui and 'F(" eof=")' in ui and 'F(" expected=")' in ui
sr = int(re.search(r'SAMPLE_RATE = (\d+)u', fixture).group(1))
dur = int(re.search(r'DURATION_MS = (\d+)u', fixture).group(1))
assert sr * dur // 1000 == 88200
print('dev.2n-r7 completion contract PASS')
