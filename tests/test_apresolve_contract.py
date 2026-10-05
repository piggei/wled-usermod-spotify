from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp = (root / "spotify" / "SpotifySessionProbe.cpp").read_text()
hdr = (root / "spotify" / "SpotifySessionProbe.h").read_text()

required = [
    'http://apresolve.spotify.com/?type=accesspoint',
    'KEY_CURRENT = "accesspoint"',
    'KEY_LEGACY = "ap_list"',
    'AP_FALLBACK = "ap.spotify.com:443"',
    'tcp.connect(host.c_str(), port, CONNECT_TIMEOUT_MS)',
    'setResolverMode("http")',
    'setResolverMode("fallback")',
]
for marker in required:
    assert marker in cpp, marker

# Credential handling is tested separately by test_ap_auth_contract.py.

# Network work must never execute directly in the WLED loop.
assert 'xTaskCreate(taskThunk' in cpp
assert 'xTaskCreatePinnedToCore' not in cpp

print('AP resolve/TCP contract checks: PASS')

assert 'NetworkClientSecure' not in cpp
assert 'WiFiClientSecure' not in cpp
assert 'resolveWithHttps' not in cpp
