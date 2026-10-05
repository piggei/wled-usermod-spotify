from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = root / "spotify" / "Aes192Software.cpp"
header_dir = root / "spotify"

harness = r'''
#include <cstdint>
#include <cstring>
#include "Aes192Software.h"
int main() {
  const uint8_t key[24] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
    0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
    0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17
  };
  uint8_t block[16] = {
    0xdd,0xa9,0x7c,0xa4,0x86,0x4c,0xdf,0xe0,
    0x6e,0xaf,0x70,0xa0,0xec,0x0d,0x71,0x91
  };
  const uint8_t expected[16] = {
    0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
    0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff
  };
  if (!SpotifyCrypto::aes192DecryptEcbInPlace(key, block, sizeof(block))) return 2;
  return std::memcmp(block, expected, sizeof(block)) == 0 ? 0 : 1;
}
'''

with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    cpp = td / "vector.cpp"
    exe = td / "vector"
    cpp.write_text(harness)
    subprocess.run([
        "g++", "-std=c++17", "-O2", "-I", str(header_dir),
        str(cpp), str(source), "-o", str(exe)
    ], check=True)
    subprocess.run([str(exe)], check=True)

print("AES-192 FIPS vector: PASS")
