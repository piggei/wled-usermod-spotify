from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp = (root / 'spotify' / 'SpotifySessionProbe.cpp').read_text()
hdr = (root / 'spotify' / 'SpotifySessionProbe.h').read_text()
ui = (root / 'usermod_spotify_connect.h').read_text()

# Measurement-only: keep the already-qualified AP identity literals unchanged.
assert 'constexpr uint64_t SPOTIFY_VERSION = 0x10800000000ULL;' in cpp
assert 'constexpr uint8_t CLIENT_PRODUCT_CLASS = 0u;' in cpp
assert 'constexpr uint8_t CLIENT_PLATFORM_CLASS = 2u;' in cpp
assert 'constexpr uint8_t AUTH_CPU_CLASS = 0u;' in cpp
assert 'constexpr uint8_t AUTH_OS_CLASS = 0u;' in cpp
assert 'constexpr const char* AUTH_SYSTEM_NAME = "wled-spotify";' in cpp
assert 'constexpr const char* AUTH_CLIENT_VERSION = "wled-spotify-dev2e-r2";' in cpp
assert 'appendVarintField(buildInfo, 10u, CLIENT_PRODUCT_CLASS)' in cpp
assert 'appendVarintField(buildInfo, 30u, CLIENT_PLATFORM_CLASS)' in cpp
assert 'appendVarintField(buildInfo, 40u, SPOTIFY_VERSION)' in cpp
assert 'appendVarintField(systemInfo, 10u, AUTH_CPU_CLASS)' in cpp
assert 'appendVarintField(systemInfo, 60u, AUTH_OS_CLASS)' in cpp
assert 'appendStringField(systemInfo, 90u, String(AUTH_SYSTEM_NAME))' in cpp
assert 'appendStringField(out, 70u, String(AUTH_CLIENT_VERSION))' in cpp

# ProductInfo capability evidence is bounded text only.
for tag in (
    'on-demand', 'high-bitrate', 'unrestricted', 'mobile',
    'prefetch-keys', 'key-memory-cache-mode', 'key-caching-max-count',
):
    assert f'copyProductField("{tag}"' in cpp

for marker in (
    'AP identity product=', 'mode=measurement-only',
    'ProductInfo keyCaps onDemand=',
    'AudioKey identityAudit requestWire=frozen fileId20+gid16+be32seq+be16zero identityMutation=none',
):
    assert marker in ui

# The qualified RequestKey byte layout remains untouched.
request = cpp[cpp.index('std::vector<uint8_t> buildAudioKeyRequest'):]
request = request[:request.index('std::vector<uint8_t> buildApStreamChunkRequest')]
assert 'out.reserve(AUDIO_FILE_ID_BYTES + TRACK_GID_BYTES + 6u);' in request
assert 'out.insert(out.end(), fileId, fileId + AUDIO_FILE_ID_BYTES);' in request
assert 'out.insert(out.end(), trackGid, trackGid + TRACK_GID_BYTES);' in request
assert 'out.push_back(0x00u);\n  out.push_back(0x00u);' in request

# r14 must not open the live decrypt consumer.
assert 'consumer=closed keyGate=' in ui
assert 'decrypt=closed' in ui
assert '../decoder/SpotifyAudioAesCtr' not in cpp
assert '../decoder/SpotifyVorbisFixturePlayer' not in cpp
assert 'USERMOD_REVISION = "r20"' in ui

print('dev.2n-r14 AudioKey identity telemetry measurement-only contract PASS')
