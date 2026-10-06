#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp = (root / "spotify/SpotifySessionProbe.cpp").read_text()
h = (root / "spotify/SpotifySessionProbe.h").read_text()
ui = (root / "usermod_spotify_connect.h").read_text()
lib = (root / "library.json").read_text()

assert '"version": "0.1.0-dev.2l-ap-stream-r1"' in lib
assert 'USERMOD_VERSION = "0.1.0-dev.2l-ap-stream"' in ui
assert 'USERMOD_REVISION = "r1"' in ui

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

# Canary is bounded to 4 KiB (1024 protocol words) and never stores media bytes.
assert "AP_STREAM_CANARY_BYTES = 4096u" in h
assert "AP_STREAM_WORD_BYTES = 4u" in h
assert "AP_STREAM_CANARY_WORDS = AP_STREAM_CANARY_BYTES / AP_STREAM_WORD_BYTES" in h
assert "AP_STREAM_TIMEOUT_MS = 5000u" in h
assert "buildApStreamChunkRequest(" in cpp and "AP_STREAM_CANARY_WORDS" in cpp
assert "apStreamDataBytes_ += payload.size() - offset" in cpp
assert "std::vector<uint8_t> apStream" not in h

# Channel routing is correlated, bounded and classifies both 0x09 and 0x0a.
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

# Native TLS experiment is not active in this build; no secret/media payload is logged.
assert "esp_http_client_perform" not in cpp
assert "esp_crt_bundle_attach" not in cpp
assert "WiFiClientSecure" not in cpp
assert "NetworkClientSecure" not in cpp
assert "AP Stream attempts=" in ui
assert "AP Stream lastError=" in ui
assert "dataBytes=" in ui and "format=" in ui
assert "audioKeyHex" not in ui
assert "AP StreamChunk 4KiB encrypted canary" in ui
print("dev.2l AP StreamChunk canary contract: PASS")
