#!/usr/bin/env python3
import gzip
import struct
import zlib

payload = (
    b'{"endpoint":"play","options":{"skip_to":{"track_uri":'
    b'"spotify:track:5ep8e1ZbIjtUajhcsskkpb","track_index":7}}}'
)
member = gzip.compress(payload, mtime=0)
assert member[:3] == b"\x1f\x8b\x08"
assert len(member) >= 18
flags = member[3]
assert flags & 0xE0 == 0
pos = 10
trailer = len(member) - 8
if flags & 0x04:
    xlen = member[pos] | (member[pos + 1] << 8)
    pos += 2 + xlen
for flag in (0x08, 0x10):
    if flags & flag:
        pos = member.index(b"\x00", pos, trailer) + 1
if flags & 0x02:
    pos += 2
raw = member[pos:trailer]
out = zlib.decompress(raw, -zlib.MAX_WBITS)
crc, isize = struct.unpack_from('<II', member, trailer)
assert len(out) == isize
assert zlib.crc32(out) & 0xFFFFFFFF == crc
assert out == payload
assert b'"endpoint":"play"' in out
assert b'"track_uri":"spotify:track:5ep8e1ZbIjtUajhcsskkpb"' in out
print("dev.2m-r9 gzip framing/raw-deflate/CRC fixture: PASS")
