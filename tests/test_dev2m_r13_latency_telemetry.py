from pathlib import Path

root = Path(__file__).resolve().parents[1]
h = (root / "spotify/SpotifySessionProbe.h").read_text()
cpp = (root / "spotify/SpotifySessionProbe.cpp").read_text()
ui = (root / "usermod_spotify_connect.h").read_text()

assert 'USERMOD_REVISION = "r7"' in ui
for symbol in [
    'spircContextResolveLastUs_', 'spircContextResolveMaxUs_',
    'spircSelectionApplyLastUs_', 'spircSelectionApplyMaxUs_',
    'metadataRequestedAtMs_', 'metadataLastRoundTripMs_', 'metadataMaxRoundTripMs_'
]:
    assert symbol in h
assert 'const uint32_t resolveStartedUs = micros();' in cpp
assert 'spircSelectionApplyLastUs_ = micros() - selectionApplyStartedUs;' in cpp
assert 'metadataRequestedAtMs_ = millis();' in cpp
assert 'metadataLastRoundTripMs_ = millis() - metadataRequestedAtMs_;' in cpp
assert 'SPIRC timing resolveLast=' in ui
assert 'maxRtt=' in ui
assert 'scan=' in ui and 'suppressed' in ui
print('dev.2m-r13 timing telemetry retained in dev.2n-r3: PASS')
