#!/usr/bin/env python3
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
s = (ROOT / "spotify/SpotifySessionProbe.cpp").read_text()
h = (ROOT / "spotify/SpotifySessionProbe.h").read_text()
u = (ROOT / "usermod_spotify_connect.h").read_text()

required = [
    'TRACK_METADATA_PREFIX = "hm://metadata/3/track/"',
    "selectedTrackGid", "selectedTrackUri", "bytesToHex",
    'String(F("GET"))', "metadataMercurySequence_", "metadataRequests_",
    "parseLegacyTrackMetadata", "parseLegacyAlbum", "decodeZigZag32",
    "metadataTitle_", "metadataArtists_", "metadataAlbum_", "metadataDurationMs_",
    "metadataCoverIdHex_", "metadataPreferredFileIdHex_",
]
for marker in required:
    assert marker in s or marker in h, marker

assert 'USERMOD_VERSION = "0.1.0-dev.2n-vorbis"' in u
assert 'USERMOD_REVISION = "r7"' in u
assert "static const char* HEX" not in s
assert "kHexDigits" in s
assert "Spotify media-key hardening" in u
assert "Metadata GET attempts=" in u
assert "TrackRef index=" in u
assert "Track title=" in u and "Track album=" in u and "Track audioFiles=" in u
assert "scope=Spotify network remains frozen at encrypted StreamChunk; independent local Ogg/Vorbis -> PCM gate enabled; AES integration remains closed" in u

# TrackRef is the public SPIRC State.track field 27, with gid field 1 and uri field 2.
assert "field == 27u" in s
assert "extractLengthDelimited(state.data() + offset, len, 1u, gid)" in s
assert "extractProtoString(state.data() + offset, len, 2u, uri)" in s

# Legacy spotify.metadata.Track fields used by the old Mercury metadata endpoint:
# name=2, album=3, artist=4, duration(sint32)=7, audio file=12.
for marker in ["field == 2u", "field == 3u", "field == 4u", "field == 7u", "field == 12u"]:
    assert marker in s
# Album cover is repeated field 9; Image.file_id=1, Image.size=2.
assert "field == 9u" in s
assert "bestCoverSize" in s
# Preferred audio file policy: OGG_VORBIS_160 enum value 1 when present.
assert "format == 1u" in s

# Metadata acquisition remains independent of later storage/CDN/decode work.
for forbidden in ["hm://keymaster/", "storage-resolve", "cdn-resolve", "VorbisDecoder"]:
    assert forbidden not in s, forbidden
assert "enqueuePcm44100" not in s
print("dev.2h track metadata contract: PASS")
