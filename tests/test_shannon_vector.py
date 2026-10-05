from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = root / 'spotify' / 'SpotifyShannon.cpp'
include = root / 'spotify'

program = r'''
#include "SpotifyShannon.h"
#include <cstdio>
#include <cstring>
#include <vector>

static bool equals(const std::vector<unsigned char>& a, const unsigned char* b, size_t n) {
  return a.size() == n && std::memcmp(a.data(), b, n) == 0;
}

int main() {
  const std::vector<unsigned char> key = {0x65,0x87,0xd8,0x8f,0x6c,0x32,0x9d,0x8a,0xe4,0x6b};
  const char* text = "My secret message";
  std::vector<unsigned char> msg(text, text + std::strlen(text));
  const unsigned char expectedCipher[] = {
    0x91,0x9d,0xa9,0xb6,0x29,0xfc,0x9c,0xdd,0x17,0x8c,0x15,0x31,0x9a,0xae,0xcc,0x6e,0xd4
  };
  const unsigned char expectedMac[] = {
    0xbe,0x7b,0xef,0x39,0xee,0xfe,0x54,0xfd,0x8d,0xb0,0xbc,0x6f,0xd5,0x30,0x35,0x19
  };

  SpotifyShannon enc;
  enc.key(key);
  enc.encrypt(msg);
  std::vector<unsigned char> mac(16, 0);
  enc.finish(mac);
  if (!equals(msg, expectedCipher, sizeof(expectedCipher)) ||
      !equals(mac, expectedMac, sizeof(expectedMac))) return 1;

  SpotifyShannon dec;
  dec.key(key);
  dec.decrypt(msg);
  std::vector<unsigned char> mac2(16, 0);
  dec.finish(mac2);
  if (std::memcmp(msg.data(), text, msg.size()) != 0 ||
      !equals(mac2, expectedMac, sizeof(expectedMac))) return 2;

  // Spotify receives the encrypted 3-byte command/length header separately
  // from the payload. Verify that split decrypt/MAC processing is identical
  // to one-shot encrypt processing for the same nonce.
  const std::vector<unsigned char> nonce = {0x00,0x00,0x00,0x00};
  std::vector<unsigned char> packet = {0xab,0x00,0x08,0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80};
  const std::vector<unsigned char> plain = packet;
  SpotifyShannon tx;
  tx.key(key);
  tx.nonce(nonce);
  tx.encrypt(packet);
  std::vector<unsigned char> packetMac(4, 0);
  tx.finish(packetMac);

  std::vector<unsigned char> header(packet.begin(), packet.begin() + 3);
  std::vector<unsigned char> payload(packet.begin() + 3, packet.end());
  SpotifyShannon rx;
  rx.key(key);
  rx.nonce(nonce);
  rx.decrypt(header);
  rx.decrypt(payload);
  std::vector<unsigned char> splitMac(4, 0);
  rx.finish(splitMac);
  std::vector<unsigned char> recovered = header;
  recovered.insert(recovered.end(), payload.begin(), payload.end());
  if (recovered != plain || splitMac != packetMac) return 3;
  return 0;
}
'''

with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    test_cpp = td / 'test.cpp'
    binary = td / 'test'
    test_cpp.write_text(program)
    subprocess.run([
        'g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
        '-I', str(include), str(source), str(test_cpp), '-o', str(binary)
    ], check=True)
    subprocess.run([str(binary)], check=True)

print('Shannon known-answer vector: PASS')
