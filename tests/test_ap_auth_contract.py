from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp = (root / 'spotify' / 'SpotifySessionProbe.cpp').read_text()
hdr = (root / 'spotify' / 'SpotifySessionProbe.h').read_text()
zc = (root / 'spotify' / 'SpotifyZeroConfProbe.h').read_text()
shannon = (root / 'spotify' / 'SpotifyShannon.cpp').read_text()

required_cpp = [
    'SPOTIFY_VERSION = 0x10800000000ULL',
    'LOGIN_REQUEST_COMMAND = 0xABu',
    'AUTH_SUCCESSFUL_COMMAND = 0xACu',
    'AUTH_DECLINED_COMMAND = 0xADu',
    'buildClientHello',
    'parseApDhPublicKey',
    'calculateDhShared',
    'challengeData.insert',
    'resultData.begin() + 20',
    'resultData.begin() + 52',
    'resultData.begin() + 84',
    'buildClientResponsePlaintext',
    'buildAuthRequest',
    'sendShannonPacket',
    'recvShannonPacket',
    'state_ = State::Authenticated',
]
for marker in required_cpp:
    assert marker in cpp, marker

assert 'const String& userName() const' in zc
assert 'const std::vector<uint8_t>& authData() const' in zc
assert 'xTaskCreate(taskThunk' in cpp
assert 'xTaskCreatePinnedToCore' not in cpp
assert 'NetworkClientSecure' not in cpp
assert 'WiFiClientSecure' not in cpp
assert 'INITIAL_CONSTANT = 0x6996c53a' in (root / 'spotify' / 'SpotifyShannon.h').read_text()
assert 'void SpotifyShannon::encrypt' in shannon
assert 'void SpotifyShannon::decrypt' in shannon
assert 'void SpotifyShannon::finish' in shannon
assert 'MAX_AP_ENCRYPTED_PACKET' in hdr

print('AP handshake/Shannon/stored-credential contract: PASS')
