#!/usr/bin/env python3
"""Compile/execute the actual r17 encrypted continuous ring with host sanitizers."""
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
#include <cstring>
#include <vector>
#include "decoder/SpotifyApContinuousRing.h"

static uint32_t fnv(uint32_t h, const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
  return h;
}

static void drain(SpotifyApContinuousRing& ring, uint32_t& hash, size_t& bytes) {
  uint8_t out[777];
  while (ring.bufferedBytes()) {
    size_t n = 0;
    assert(ring.pop(out, sizeof(out), n));
    assert(n > 0);
    hash = fnv(hash, out, n);
    bytes += n;
  }
}

int main() {
  std::vector<uint8_t> data(65536u);
  for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>((i * 37u + 11u) & 0xffu);
  const uint32_t expected = fnv(2166136261u, data.data(), data.size());

  // Exact r17 target: 16 x 4096 bytes, drained continuously.
  SpotifyApContinuousRing ring;
  assert(ring.begin(0u, data.size()));
  uint32_t consumerHash = 2166136261u;
  size_t consumerBytes = 0u;
  for (size_t base = 0; base < data.size(); base += 4096u) {
    size_t inRange = 0u;
    const size_t pattern[] = {31u, 997u, 2048u, 1020u};
    for (size_t part : pattern) {
      assert(inRange + part <= 4096u);
      assert(ring.push(static_cast<uint32_t>(base + inRange), data.data() + base + inRange, part));
      inRange += part;
      drain(ring, consumerHash, consumerBytes);
    }
    assert(inRange == 4096u);
  }
  assert(ring.finishProducer());
  assert(ring.eof());
  assert(ring.producedBytes() == 65536u);
  assert(ring.consumedBytes() == 65536u);
  assert(consumerBytes == 65536u);
  assert(consumerHash == expected);
  assert(ring.highWaterBytes() <= SpotifyApContinuousRing::kCapacityBytes);
  assert(ring.backpressureEvents() == 0u);
  assert(ring.gapErrors() == 0u && ring.duplicateErrors() == 0u && ring.producerErrors() == 0u);

  // Gap is classified and invalidates the transfer.
  assert(ring.begin(100u, 16u));
  assert(!ring.push(101u, data.data(), 4u));
  assert(ring.gapErrors() == 1u && !ring.valid());

  // Duplicate/backward offset is independently classified.
  assert(ring.begin(100u, 16u));
  assert(ring.push(100u, data.data(), 4u));
  assert(!ring.push(102u, data.data() + 4u, 4u));
  assert(ring.duplicateErrors() == 1u && !ring.valid());

  // Backpressure never overwrites unread bytes; after draining, producer can continue.
  std::vector<uint8_t> larger(65537u, 0x5au);
  assert(ring.begin(0u, larger.size()));
  assert(ring.push(0u, larger.data(), 65536u));
  assert(!ring.push(65536u, larger.data() + 65536u, 1u));
  assert(ring.backpressureEvents() == 1u && ring.valid());
  uint8_t one[1]; size_t n = 0u;
  assert(ring.pop(one, sizeof(one), n) && n == 1u);
  assert(ring.push(65536u, larger.data() + 65536u, 1u));
  while (ring.bufferedBytes()) { uint8_t buf[4096]; assert(ring.pop(buf, sizeof(buf), n)); assert(n); }
  assert(ring.finishProducer() && ring.eof());

  // Real wrap-around: producer continues after consumer frees earlier capacity.
  std::vector<uint8_t> wrap(70000u);
  for (size_t i = 0; i < wrap.size(); ++i) wrap[i] = static_cast<uint8_t>(i ^ (i >> 8));
  assert(ring.begin(5000u, wrap.size()));
  assert(ring.push(5000u, wrap.data(), 60000u));
  std::vector<uint8_t> first(50000u); size_t got = 0u, total = 0u;
  while (total < first.size()) {
    assert(ring.pop(first.data() + total, first.size() - total, got)); assert(got); total += got;
  }
  assert(std::equal(first.begin(), first.end(), wrap.begin()));
  assert(ring.push(65000u, wrap.data() + 60000u, 10000u));
  std::vector<uint8_t> rest(20000u); total = 0u;
  while (total < rest.size()) {
    assert(ring.pop(rest.data() + total, rest.size() - total, got)); assert(got); total += got;
  }
  assert(std::equal(rest.begin(), rest.end(), wrap.begin() + 50000u));
  assert(ring.finishProducer() && ring.eof());
  assert(ring.highWaterBytes() <= SpotifyApContinuousRing::kCapacityBytes);

  // Premature finish is rejected.
  assert(ring.begin(0u, 8u));
  assert(ring.push(0u, data.data(), 4u));
  assert(!ring.finishProducer());
  assert(!ring.valid());
  return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='spotify-r17-ring-') as tmp:
    p = Path(tmp)
    unit = p / 'ring_test.cpp'
    binary = p / 'ring_test'
    unit.write_text(unit_src)
    flags = ['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic']
    if os.environ.get('SPOTIFY_R17_SANITIZERS') == '1':
        flags += ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
    cmd = shlex.split(os.environ.get('CXX','g++')) + flags + [
        '-I', str(root), str(unit), str(root/'decoder/SpotifyApContinuousRing.cpp'), '-o', str(binary)]
    subprocess.run(cmd, check=True, timeout=60)
    subprocess.run([str(binary)], check=True, timeout=30)
print('r17 continuous ring native PASS')
