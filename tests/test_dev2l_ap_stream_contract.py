#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp = (root / "spotify/SpotifySessionProbe.cpp").read_text()
h = (root / "spotify/SpotifySessionProbe.h").read_text()
ui = (root / "usermod_spotify_connect.h").read_text()
lib = (root / "library.json").read_text()

assert '"version": "0.1.0-dev.2n-vorbis-r7"' in lib
assert 'USERMOD_VERSION = "0.1.0-dev.2n-vorbis"' in ui
assert 'USERMOD_REVISION = "r7"' in ui

# Historical AP media channel wire contract, implemented independently.
assert "STREAM_CHUNK_REQUEST_COMMAND = 0x08u" in cpp
assert "STREAM_CHUNK_SUCCESS_COMMAND = 0x09u" in cpp
assert "STREAM_CHUNK_FAILURE_COMMAND = 0x0Au" in cpp
assert "buildApStreamChunkRequest" in cpp
assert "out.reserve(46u)" in cpp
assert "appendBe16(out, channelId)" in cpp
assert "out.push_back(0x00u)" in cpp and "out.push_back(0x01u)" in cpp
assert "appendBe32(out, 0x00009C40u)" in cpp
assert "appendBe32(out, 0x00020000u)" in cpp
assert "out.insert(out.end(), fileId, fileId + AUDIO_FILE_ID_BYTES)" in cpp
assert "appendBe32(out, offsetWords + sizeWords)" in cpp

# r2 performs three serial bounded 4 KiB ranges, with a new channel per range.
assert "AP_STREAM_CANARY_BYTES = 4096u" in h
assert "AP_STREAM_PROBE_COUNT = 3u" in h
assert "AP_STREAM_WORD_BYTES = 4u" in h
assert "AP_STREAM_CANARY_WORDS = AP_STREAM_CANARY_BYTES / AP_STREAM_WORD_BYTES" in h
assert "AP_STREAM_TIMEOUT_MS = 5000u" in h
assert "sendApStreamProbe" in cpp
assert "probeIndex) * AP_STREAM_CANARY_WORDS" in cpp
assert "apStreamCompletedProbes_ < AP_STREAM_PROBE_COUNT" in cpp
assert "apStreamDataBytes_ += packetDataBytes" in cpp
assert "std::vector<uint8_t> apStream" not in h

# A range is successful on channel close after enough data, avoiding r1's false stale close packet.
assert "apStreamCurrentDataBytes_ >= AP_STREAM_CANARY_BYTES" in cpp
assert "apStreamLastCompletedChannelId_ = apStreamChannelId_" in cpp
assert "++apStreamPostCompletePackets_" in cpp
assert "AP StreamChunk closed short" in cpp

# Channel routing remains correlated and classifies 0x09/0x0a/timeout separately.
assert "channelId != apStreamChannelId_" in cpp
assert "++apStreamStalePackets_" in cpp
assert "readBe16At(payload, 2u)" in cpp
assert "Spotify AP StreamChunk channel error" in cpp
assert "AP StreamChunk response timeout" in cpp
assert "headerId == 0x03u" in cpp
assert "apStreamReportedFileBytes_" in cpp

# Existing key path remains untouched and the AP canary starts only after key-scan terminal state.
assert "REQUEST_KEY_COMMAND = 0x0Cu" in cpp
assert "AES_KEY_COMMAND = 0x0Du" in cpp
assert "AES_KEY_ERROR_COMMAND = 0x0Eu" in cpp
assert "startApStreamCanary" in cpp
assert "audioKeyCandidateCount_ == 0u" in cpp

# Native TLS experiment remains inactive; no secret/media payload is logged.
assert "esp_http_client_perform" not in cpp
assert "esp_crt_bundle_attach" not in cpp
assert "WiFiClientSecure" not in cpp
assert "NetworkClientSecure" not in cpp
assert "AP Stream attempts=" in ui
assert "postComplete=" in ui
assert "totalRequested=" in ui and "probe=" in ui and "offset=" in ui
assert "dataBytes=" in ui and "format=" in ui
assert "audioKeyHex" not in ui
assert "AP Stream attempts=" in ui and "probe=" in ui and "totalRequested=" in ui

# Keep postbuild manifest literals synchronized with the runtime scope string.
manifest = (root / "tests/release_checks.tsv").read_text()
expected_scope = "scope=Spotify network remains frozen at encrypted StreamChunk; independent local Ogg/Vorbis -> PCM gate enabled; AES integration remains closed"
assert manifest.count(expected_scope) >= 3  # prebuild SESSION_SCOPE + postbuild FW_SESSION/FW_AUDIO_KEY_SCOPE
assert "AP StreamChunk 4KiB encrypted canary; decrypt/decoder remain closed" not in manifest
print("dev.2l-r2 sequential AP StreamChunk canary contract: PASS")
