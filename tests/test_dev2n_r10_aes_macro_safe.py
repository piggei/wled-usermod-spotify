#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
hdr = (ROOT / "decoder/SpotifyAudioAesCtr.h").read_text()
cpp = (ROOT / "decoder/SpotifyAudioAesCtr.cpp").read_text()
player = (ROOT / "decoder/SpotifyVorbisFixturePlayer.cpp").read_text()
ui = (ROOT / "usermod_spotify_connect.h").read_text()

# ESP32-S3 HAL defines IV_BYTES as a macro.  Do not use that token as a C++
# member identifier after including mbedtls/aes.h.
assert "static constexpr size_t IV_BYTES" not in hdr
assert "SpotifyAudioAesCtr::IV_BYTES" not in cpp
assert "kIvBytes = 16u" in hdr
assert "FIXED_IV[kIvBytes]" in hdr
assert "nonceCounter_[kIvBytes]" in hdr
assert "streamBlock_[kIvBytes]" in hdr

# Keep the key-size constant equally macro-resistant and preserve the telemetry.
assert "kKeyBytes = 16u" in hdr
assert "SpotifyAudioAesCtr::kKeyBytes" in player
assert 'USERMOD_REVISION = "r20"' in ui
print("dev.2n-r10 AES macro-safe identifiers: PASS")
