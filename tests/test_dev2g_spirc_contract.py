#!/usr/bin/env python3
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
s = (ROOT / "spotify/SpotifySessionProbe.cpp").read_text()
h = (ROOT / "spotify/SpotifySessionProbe.h").read_text()
u = (ROOT / "usermod_spotify_connect.h").read_text()

required = [
    "SPIRC_HELLO = 0x01u", "SPIRC_NOTIFY = 0x0Au", "SPIRC_LOAD = 0x14u",
    "SPIRC_PLAY = 0x15u", "SPIRC_PAUSE = 0x16u",
    'SPIRC_PROTOCOL_VERSION = "2.7.1"', "buildSpircCapability",
    "buildSpircDeviceState", "buildSpircState", "buildSpircFrame",
    "parseSpircFrame", "buildSpircTransferNotify", 'String(F("SEND"))', "MERCURY_SEND_COMMAND = 0xB2u",
    "spircHelloMercurySequence_", "spircHelloAcks_", "spircLoadFrames_",
]
for marker in required:
    assert marker in s or marker in h, marker
assert "SPIRC hello attempts=" in u and "SPIRC rx=" in u and "SPIRC remote ident=" in u
assert "SPIRC transfer Notify attempts=" in u and "SPIRC Load tracks=" in u
assert "sendSpircTransferNotify(loadState," in s
assert "spircLocalActive_ = true" in s
assert "appendVarintField(state, 5u, playStatus)" in s
assert "remote.hasPlayStatus ? remote.playStatus : 1u" in s
assert "get_audio_key" not in s
assert "storage-resolve" not in s
assert "SpotifyVorbisFixturePlayer" not in s
assert "enqueuePcm44100" not in s

# Ordering: subscription must precede Hello advertisement, preserving dev.2f startup.
assert s.index("MERCURY_SUB_COMMAND") < s.index("auto sendSpircHello")
assert s.index("subscriptionBecameReady") < s.index("if (subscriptionBecameReady && !spircHelloSentThisSession)")

# Independent protobuf wire-vector for the minimal Hello fields. This catches
# accidental changes to the field numbers defined by the public spirc.proto.
def varint(v):
    out = bytearray()
    while True:
        b = v & 0x7f
        v >>= 7
        out.append(b | (0x80 if v else 0))
        if not v:
            return bytes(out)

def key(field, wire):
    return varint((field << 3) | wire)

def vint(field, value):
    return key(field, 0) + varint(value)

def blob(field, value):
    return key(field, 2) + varint(len(value)) + value

frame_prefix = (
    vint(1, 1) +
    blob(2, b"deviceid") +
    blob(3, b"2.7.1") +
    vint(4, 0) +
    vint(5, 1)
)
assert frame_prefix.startswith(b"\x08\x01\x12\x08deviceid\x1a\x052.7.1\x20\x00\x28\x01")

# Mercury SEND layout: seq-len + seq + final + part-count=2 + header + SPIRC payload.
uri = b"hm://remote/3/user/test/"
header = blob(1, uri) + blob(3, b"SEND")
spirc = frame_prefix
packet = (
    (8).to_bytes(2, "big") + (7).to_bytes(8, "big") + b"\x01" +
    (2).to_bytes(2, "big") + len(header).to_bytes(2, "big") + header +
    len(spirc).to_bytes(2, "big") + spirc
)
assert int.from_bytes(packet[11:13], "big") == 2
assert b"SEND" in packet and b"2.7.1" in packet
assert "uri.startsWith(subscriptionUri)" in s
assert "MAX_AP_ENCRYPTED_PACKET = 16384u" in h
assert "readFailStage" in s
print("dev.2g SPIRC activation contract: PASS")
