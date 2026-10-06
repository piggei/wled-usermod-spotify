#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp = (root / "spotify/SpotifySessionProbe.cpp").read_text()
ui = (root / "usermod_spotify_connect.h").read_text()

ALPHABET = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"


def spotify_uri_from_gid(gid: bytes) -> str:
    assert len(gid) == 16
    value = int.from_bytes(gid, "big")
    out = ["0"] * 22
    for i in range(21, -1, -1):
        value, rem = divmod(value, 62)
        out[i] = ALPHABET[rem]
    return "spotify:track:" + "".join(out)


def varint(value: int) -> bytes:
    out = bytearray()
    while value >= 0x80:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    out.append(value)
    return bytes(out)


def gid_only_track_ref(gid: bytes) -> bytes:
    # protobuf TrackRef field 1, wire type 2, length 16: 1 + 1 + 16 = 18 bytes.
    return varint((1 << 3) | 2) + varint(len(gid)) + gid


# Exact real-hardware r11 observation from /json/info: this GID was exposed as the
# corresponding Spotify URI even though the retained field-27 queue measured exactly
# 18 bytes per TrackRef, consistent with field-1/GID-only entries.
gid = bytes.fromhex("082fb9e25e8e48a79caa41a924af1574")
uri = spotify_uri_from_gid(gid)
assert uri == "spotify:track:0frKt739Ov9vvKS3JRu5Vi"
ref = gid_only_track_ref(gid)
assert len(ref) == 18
assert ref[:2] == bytes((0x0A, 0x10))
assert uri == spotify_uri_from_gid(ref[2:])

# Source contract: GID-only TrackRefs are canonicalized before URI matching.
assert "String canonicalSpircTrackUri" in cpp
assert 'uri.startsWith(F("spotify:track:"))' in cpp
assert "gid.size() == TRACK_GID_BYTES" in cpp
assert "return spotifyTrackUriFromGid(gid.data(), gid.size());" in cpp
assert "const String canonicalUri = canonicalSpircTrackUri(spircStateTrackRefs_[i]);" in cpp

# Safety contract: modern context track_index is diagnostic/advisory, never a naked
# retained-queue selector. It can only be counted as validated when it agrees with an
# identity-resolved index from URI/UID.
assert "objectIdentityResolved && idxValue == objectIdentityIndex" in cpp
assert "++spircContextSkipIndexValidated_;" in cpp
assert "++spircContextSkipIndexIgnored_;" in cpp
assert "consider(idxValue);" not in cpp
assert "outIndex = info.contextPlayerTargetIndex;" not in cpp
assert "outIndex = decodedIndex;" not in cpp

# Multi-skip convergence must run before the first-target JSON shortcut.
inflated = cpp.index('if (decodedEncoding == F("json") &&')
first_uri = cpp.index('decodedEncoding != F("json") && decodedUri.length()')
assert inflated < first_uri

assert "SPIRC contextQueue refs=" in ui
assert "indexValidated=" in ui and "indexIgnored=" in ui

print("dev.2m-r12 GID-only TrackRef canonical identity fixture: PASS")
