#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp = (root / "spotify/SpotifySessionProbe.cpp").read_text()
ui = (root / "usermod_spotify_connect.h").read_text()
doc = (root / "docs/DEV2K_SPCLIENT_TLS.md").read_text()

# dev.2k is historical evidence: the real target linked esp-tls but had no
# definitions for mbedtls_ssl_* in any framework archive. Do not retry the
# same native TLS path from active runtime code.
assert "mbedtls_ssl_init" in doc
assert "native TLS unavailable" in doc
assert "esp_http_client_perform" not in cpp
assert "esp_crt_bundle_attach" not in cpp
assert "SpClient TLS attempts=" not in ui
assert "WiFiClientSecure.h" not in cpp
assert "NetworkClientSecure.h" not in cpp
print("dev.2k target TLS classification guard: PASS")
