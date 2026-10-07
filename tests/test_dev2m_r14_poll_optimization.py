from pathlib import Path

root = Path(__file__).resolve().parents[1]
h = (root / "spotify/SpotifySessionProbe.h").read_text()
ui = (root / "usermod_spotify_connect.h").read_text()
cpp = (root / "spotify/SpotifySessionProbe.cpp").read_text()

assert 'USERMOD_REVISION = "r20"' in ui
assert 'SESSION_POLL_MS = 50u' in h
assert 'delay(SESSION_POLL_MS);' in cpp
assert 'poll=50ms' in ui
assert 'SESSION_POLL_MS = 250u' not in h
# r14 is deliberately scheduling-only: the qualified selection resolver and timing telemetry stay present.
for symbol in [
    'spircContextResolveLastUs_', 'spircContextResolveMaxUs_',
    'spircSelectionApplyLastUs_', 'spircSelectionApplyMaxUs_'
]:
    assert symbol in h
print('dev.2m-r14 conservative 50 ms AP receive poll contract: PASS')
