#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp = (root / "spotify/SpotifySessionProbe.cpp").read_text()
h = (root / "spotify/SpotifySessionProbe.h").read_text()
ui = (root / "usermod_spotify_connect.h").read_text()
lib = (root / "library.json").read_text()

assert '"version": "0.1.0-dev.2n-vorbis-r17"' in lib
assert 'USERMOD_VERSION = "0.1.0-dev.2n-vorbis"' in ui
assert 'USERMOD_REVISION = "r17"' in ui

# ProductInfo is retained from dev.2j on the AP live channel. Its tolerant bounded
# scalar XML tag scanner and exports only selected non-sensitive attributes.
assert "PRODUCT_INFO_COMMAND = 0x50u" in cpp
assert "delimiter != '>'" in cpp and "while (gt < payload.size()" in cpp
assert 'copyProductField("type"' in cpp
assert 'copyProductField("catalogue"' in cpp
assert 'copyProductField("player-license"' in cpp
assert 'copyProductField("head-files"' in cpp
assert 'extractXmlTag(payload, "head-files-url", headTemplate)' in cpp
assert "productInfoHash_" in h and "2166136261u" in cpp
assert "headFileTemplate_[192]" in h
assert "ProductInfo packets=" in ui and "ProductInfo attrs type=" in ui
assert "headUrl=" in ui and "scheme=" in ui and "xml=" in ui
assert "headFileTemplate_" not in ui

# The historical media-head canary remains bounded and plain-HTTP only. It reads at most 4 KiB,
# requests a byte range, records status/length/signature and never stores/logs body.
assert "MEDIA_HEAD_MAX_BYTES = 4096u" in h
assert "MEDIA_HEAD_TIMEOUT_MS = 5000u" in h
assert 'String(F("bytes=0-"))' in cpp
assert "HTTP_CODE_PARTIAL_CONTENT" in cpp
assert "mediaHeadBytes_ < MEDIA_HEAD_MAX_BYTES" in cpp
assert "firstBytes[0] == 'O'" in cpp and "firstBytes[3] == 'S'" in cpp
assert 'headTemplate.startsWith(F("https://"))' in cpp
assert '"HTTPS head-files-url unsupported by this gate"' in cpp
assert "NetworkClientSecure" not in cpp
assert "WiFiClientSecure" not in cpp

# dev.2l no longer triggers the legacy media-head path after the key scan; the
# code remains available only as preserved diagnostic history. RequestKey stays present.
assert "fetchMediaHeadCandidate" in cpp
assert "startApStreamCanary" in cpp
assert "REQUEST_KEY_COMMAND = 0x0Cu" in cpp
assert "AES_KEY_COMMAND = 0x0Du" in cpp
assert "AES_KEY_ERROR_COMMAND = 0x0Eu" in cpp

# Diagnostics contain only bounded counters/status, not reusable credentials,
# media key material, file bytes, or the expanded media URL.
assert "MediaHead attempts=" in ui
assert "MediaHead lastError=" in ui
assert "unsupportedScheme=" in ui
assert "audioKeyHex" not in ui
assert "expanded head-files-url" not in ui
assert "scope=live AP encrypted canary retained transiently behind MediaChunkSource; hard key gate + live decrypt/decoder consumer remain closed" in ui
print("dev.2j retained ProductInfo/media-head contract: PASS")
