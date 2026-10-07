#!/usr/bin/env python3
"""Compile/execute the actual r19 bounded ring over 1 MiB + EOF-tail shapes."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
unit_src = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>
#include "decoder/SpotifyApContinuousRing.h"

static uint32_t fnv(uint32_t h, const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
  return h;
}

static void drain(SpotifyApContinuousRing& ring, uint32_t& hash, size_t& bytes) {
  uint8_t out[512];
  while (ring.bufferedBytes()) {
    size_t n = 0u;
    assert(ring.pop(out, sizeof(out), n));
    assert(n > 0u);
    hash = fnv(hash, out, n);
    bytes += n;
  }
}

int main() {
  constexpr uint32_t start = 65536u;
  constexpr size_t total = 1024u * 1024u;
  std::vector<uint8_t> data(total);
  for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>((i * 19u + (i >> 7) + 3u) & 0xffu);
  const uint32_t expected = fnv(2166136261u, data.data(), data.size());

  SpotifyApContinuousRing ring;
  assert(ring.begin(start, total));
  uint32_t consumerHash = 2166136261u;
  size_t consumerBytes = 0u;
  for (size_t range = 0; range < 256u; ++range) {
    const size_t base = range * 4096u;
    assert(ring.push(start + static_cast<uint32_t>(base), data.data() + base, 4096u));
    drain(ring, consumerHash, consumerBytes);
  }
  assert(ring.finishProducer());
  assert(ring.eof());
  assert(ring.producedBytes() == total);
  assert(ring.consumedBytes() == total);
  assert(consumerBytes == total);
  assert(consumerHash == expected);
  assert(ring.writeWraps() == 16u);
  assert(ring.readWraps() == 16u);
  assert(ring.highWaterBytes() == 4096u);
  assert(ring.backpressureEvents() == 0u);
  assert(ring.gapErrors() == 0u && ring.duplicateErrors() == 0u && ring.producerErrors() == 0u);

  // Exact EOF tail shape from the observed Perfect Day file size: 4,380,916 B,
  // final 4096-aligned start 4,378,624 and 2,292 bytes to the exact file boundary.
  constexpr uint32_t fileBytes = 4380916u;
  constexpr uint32_t tailStart = 4378624u;
  constexpr size_t tailBytes = fileBytes - tailStart;
  static_assert(tailBytes == 2292u, "fixture tail geometry");
  std::vector<uint8_t> tail(tailBytes);
  for (size_t i = 0; i < tail.size(); ++i) tail[i] = static_cast<uint8_t>(i * 7u + 1u);
  assert(ring.begin(tailStart, tailBytes));
  uint32_t tailHash = 2166136261u;
  size_t tailConsumed = 0u;
  assert(ring.push(tailStart, tail.data(), 1000u));
  drain(ring, tailHash, tailConsumed);
  assert(ring.push(tailStart + 1000u, tail.data() + 1000u, tailBytes - 1000u));
  drain(ring, tailHash, tailConsumed);
  assert(ring.finishProducer());
  assert(ring.eof());
  assert(tailConsumed == tailBytes);
  assert(tailStart + tailConsumed == fileBytes);

  // Cancellation/invalidation must discard unread bytes and make the transfer unusable.
  assert(ring.begin(start, total));
  assert(ring.push(start, data.data(), 4096u));
  assert(ring.bufferedBytes() == 4096u);
  ring.invalidate();
  assert(!ring.valid());
  assert(!ring.active());
  assert(ring.bufferedBytes() == 0u);
  size_t n = 123u; uint8_t out[8];
  assert(!ring.pop(out, sizeof(out), n));
  return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='spotify-r19-ring-') as tmp:
    p = Path(tmp)
    unit = p / 'ring_test.cpp'
    binary = p / 'ring_test'
    unit.write_text(unit_src)
    flags = ['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic']
    if os.environ.get('SPOTIFY_R19_SANITIZERS') == '1':
        flags += ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
    cmd = shlex.split(os.environ.get('CXX','g++')) + flags + [
        '-I', str(root), str(unit), str(root/'decoder/SpotifyApContinuousRing.cpp'), '-o', str(binary)]
    subprocess.run(cmd, check=True, timeout=60)
    subprocess.run([str(binary)], check=True, timeout=30)
print('r19 extended ring native PASS: 1 MiB/256 ranges, 16 ring address wraps, exact EOF tail, cancellation invalidation')
