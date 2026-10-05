#!/usr/bin/env python3
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
s = (ROOT / "spotify/SpotifySessionProbe.cpp").read_text()
h = (ROOT / "spotify/SpotifySessionProbe.h").read_text()
u = (ROOT / "usermod_spotify_connect.h").read_text()
required = [
    "PING_COMMAND = 0x04u", "PONG_COMMAND = 0x49u", "COUNTRY_CODE_COMMAND = 0x1Bu",
    "MERCURY_SEND_COMMAND = 0xB2u", "MERCURY_SUB_COMMAND = 0xB3u",
    "MERCURY_EVENT_COMMAND = 0xB5u", "buildMercuryRequest", "parseMercuryEnvelope",
    'String(F("SUB"))', 'String(F("hm://remote/3/user/"))', "SESSION_RX_TIMEOUT_MS",
    "Spotify reconnect limit reached", "sendShannonPacket(tcp, sendCipher, sendNonce, PONG_COMMAND",
]
for marker in required:
    assert marker in s or marker in h, marker
assert "Mercury SUB attempts=" in u and "keepalive ping=" in u
assert "NetworkClientSecure" not in s
assert s.index("if (liveCommand == PING_COMMAND)") < s.index("mercurySubscriptionSequence_ = mercurySequence_++")
assert "subscriptionSent && isSpircSubscriptionUri(uri)" in s
assert "tcp.setNoDelay" not in s
assert "while (!stopRequested_ && WiFi.status() != WL_CONNECTED)" in s
assert 'String(F("hm://remote/user/"))' not in s

# Mercury envelope interoperability vector, matching cspot's documented layout.
def varint(v):
    out=[]
    while True:
        b=v & 0x7f; v >>= 7
        out.append(b | (0x80 if v else 0))
        if not v: return bytes(out)
def field(n, b):
    return varint((n<<3)|2) + varint(len(b)) + b
uri=b"hm://remote/3/user/test/"
header=field(1, uri)+field(3,b"SUB")
packet=(8).to_bytes(2,"big")+(0).to_bytes(8,"big")+b"\x01"+(1).to_bytes(2,"big")+len(header).to_bytes(2,"big")+header
assert packet[:15] == b"\x00\x08" + b"\x00"*8 + b"\x01\x00\x01" + len(header).to_bytes(2,"big")
assert b"\x0a" in header and b"\x1a\x03SUB" in header
print("dev.2f persistent session / Mercury contract: PASS")
