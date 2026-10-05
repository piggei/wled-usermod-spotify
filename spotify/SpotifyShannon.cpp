#include "SpotifyShannon.h"

#include <algorithm>

uint32_t SpotifyShannon::rotateLeft(uint32_t value, unsigned int count) {
  count &= 31u;
  if (count == 0u) return value;
  return (value << count) | (value >> (32u - count));
}

uint32_t SpotifyShannon::nonlinearA(uint32_t value) {
  value ^= rotateLeft(value, 5u) | rotateLeft(value, 7u);
  value ^= rotateLeft(value, 19u) | rotateLeft(value, 22u);
  return value;
}

uint32_t SpotifyShannon::nonlinearB(uint32_t value) {
  value ^= rotateLeft(value, 7u) | rotateLeft(value, 22u);
  value ^= rotateLeft(value, 5u) | rotateLeft(value, 19u);
  return value;
}

uint32_t SpotifyShannon::loadLe32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8u) |
         (static_cast<uint32_t>(bytes[2]) << 16u) |
         (static_cast<uint32_t>(bytes[3]) << 24u);
}

void SpotifyShannon::storeLe32(uint32_t value, uint8_t* bytes) {
  bytes[0] = static_cast<uint8_t>(value);
  bytes[1] = static_cast<uint8_t>(value >> 8u);
  bytes[2] = static_cast<uint8_t>(value >> 16u);
  bytes[3] = static_cast<uint8_t>(value >> 24u);
}

void SpotifyShannon::clock() {
  uint32_t feedback = state_[12] ^ state_[13] ^ constant_;
  feedback = nonlinearA(feedback) ^ rotateLeft(state_[0], 1u);

  for (size_t i = 1u; i < WORDS; ++i) state_[i - 1u] = state_[i];
  state_[WORDS - 1u] = feedback;

  const uint32_t tap = nonlinearB(state_[2] ^ state_[15]);
  state_[0] ^= tap;
  streamWord_ = tap ^ state_[8] ^ state_[12];
}

void SpotifyShannon::checksumWord(uint32_t word) {
  const uint32_t feedback = checksum_[0] ^ checksum_[2] ^ checksum_[15] ^ word;
  for (size_t i = 1u; i < WORDS; ++i) checksum_[i - 1u] = checksum_[i];
  checksum_[WORDS - 1u] = feedback;
}

void SpotifyShannon::authenticateWord(uint32_t word) {
  checksumWord(word);
  state_[KEY_INJECT_WORD] ^= word;
}

void SpotifyShannon::seedFibonacci() {
  state_[0] = 1u;
  state_[1] = 1u;
  for (size_t i = 2u; i < WORDS; ++i) state_[i] = state_[i - 1u] + state_[i - 2u];
  constant_ = INITIAL_CONSTANT;
  streamWord_ = 0u;
}

void SpotifyShannon::diffuse() {
  for (size_t i = 0u; i < WORDS; ++i) clock();
}

void SpotifyShannon::absorb(const uint8_t* bytes, size_t length) {
  size_t offset = 0u;
  while (offset + 4u <= length) {
    state_[KEY_INJECT_WORD] ^= loadLe32(bytes + offset);
    clock();
    offset += 4u;
  }

  if (offset < length) {
    uint8_t tail[4] = {0u, 0u, 0u, 0u};
    const size_t count = length - offset;
    std::copy(bytes + offset, bytes + offset + count, tail);
    state_[KEY_INJECT_WORD] ^= loadLe32(tail);
    clock();
  }

  state_[KEY_INJECT_WORD] ^= static_cast<uint32_t>(length);
  clock();

  for (size_t i = 0u; i < WORDS; ++i) checksum_[i] = state_[i];
  diffuse();
  for (size_t i = 0u; i < WORDS; ++i) state_[i] ^= checksum_[i];
}

void SpotifyShannon::rememberKeyedState() {
  for (size_t i = 0u; i < WORDS; ++i) keyedState_[i] = state_[i];
}

void SpotifyShannon::restoreKeyedState() {
  for (size_t i = 0u; i < WORDS; ++i) state_[i] = keyedState_[i];
}

void SpotifyShannon::clearPartial() {
  partialMacWord_ = 0u;
  partialBits_ = 0u;
}

void SpotifyShannon::key(const std::vector<uint8_t>& bytes) {
  seedFibonacci();
  absorb(bytes.data(), bytes.size());
  constant_ = state_[0];
  rememberKeyedState();
  clearPartial();
}

void SpotifyShannon::nonce(const std::vector<uint8_t>& bytes) {
  restoreKeyedState();
  constant_ = INITIAL_CONSTANT;
  absorb(bytes.data(), bytes.size());
  constant_ = state_[0];
  clearPartial();
}

void SpotifyShannon::stream(std::vector<uint8_t>& buffer) {
  size_t offset = 0u;

  while (partialBits_ != 0u && offset < buffer.size()) {
    buffer[offset++] ^= static_cast<uint8_t>(streamWord_ & 0xffu);
    streamWord_ >>= 8u;
    partialBits_ -= 8u;
  }

  while (offset + 4u <= buffer.size()) {
    clock();
    uint32_t word = loadLe32(buffer.data() + offset) ^ streamWord_;
    storeLe32(word, buffer.data() + offset);
    offset += 4u;
  }

  if (offset < buffer.size()) {
    clock();
    partialBits_ = 32u;
    while (offset < buffer.size()) {
      buffer[offset++] ^= static_cast<uint8_t>(streamWord_ & 0xffu);
      streamWord_ >>= 8u;
      partialBits_ -= 8u;
    }
  }
}

void SpotifyShannon::macOnly(std::vector<uint8_t>& buffer) {
  size_t offset = 0u;

  if (partialBits_ != 0u) {
    while (partialBits_ != 0u && offset < buffer.size()) {
      partialMacWord_ ^= static_cast<uint32_t>(buffer[offset++]) << (32u - partialBits_);
      partialBits_ -= 8u;
    }
    if (partialBits_ != 0u) return;
    authenticateWord(partialMacWord_);
  }

  while (offset + 4u <= buffer.size()) {
    clock();
    authenticateWord(loadLe32(buffer.data() + offset));
    offset += 4u;
  }

  if (offset < buffer.size()) {
    clock();
    partialMacWord_ = 0u;
    partialBits_ = 32u;
    while (offset < buffer.size()) {
      partialMacWord_ ^= static_cast<uint32_t>(buffer[offset++]) << (32u - partialBits_);
      partialBits_ -= 8u;
    }
  }
}

void SpotifyShannon::encrypt(std::vector<uint8_t>& buffer) {
  size_t offset = 0u;

  if (partialBits_ != 0u) {
    while (partialBits_ != 0u && offset < buffer.size()) {
      const unsigned int shift = 32u - partialBits_;
      const uint8_t plain = buffer[offset];
      partialMacWord_ ^= static_cast<uint32_t>(plain) << shift;
      buffer[offset] = plain ^ static_cast<uint8_t>((streamWord_ >> shift) & 0xffu);
      ++offset;
      partialBits_ -= 8u;
    }
    if (partialBits_ != 0u) return;
    authenticateWord(partialMacWord_);
  }

  while (offset + 4u <= buffer.size()) {
    clock();
    const uint32_t plain = loadLe32(buffer.data() + offset);
    authenticateWord(plain);
    storeLe32(plain ^ streamWord_, buffer.data() + offset);
    offset += 4u;
  }

  if (offset < buffer.size()) {
    clock();
    partialMacWord_ = 0u;
    partialBits_ = 32u;
    while (offset < buffer.size()) {
      const unsigned int shift = 32u - partialBits_;
      const uint8_t plain = buffer[offset];
      partialMacWord_ ^= static_cast<uint32_t>(plain) << shift;
      buffer[offset] = plain ^ static_cast<uint8_t>((streamWord_ >> shift) & 0xffu);
      ++offset;
      partialBits_ -= 8u;
    }
  }
}

void SpotifyShannon::decrypt(std::vector<uint8_t>& buffer) {
  size_t offset = 0u;

  if (partialBits_ != 0u) {
    while (partialBits_ != 0u && offset < buffer.size()) {
      const unsigned int shift = 32u - partialBits_;
      buffer[offset] ^= static_cast<uint8_t>((streamWord_ >> shift) & 0xffu);
      partialMacWord_ ^= static_cast<uint32_t>(buffer[offset]) << shift;
      ++offset;
      partialBits_ -= 8u;
    }
    if (partialBits_ != 0u) return;
    authenticateWord(partialMacWord_);
  }

  while (offset + 4u <= buffer.size()) {
    clock();
    const uint32_t plain = loadLe32(buffer.data() + offset) ^ streamWord_;
    authenticateWord(plain);
    storeLe32(plain, buffer.data() + offset);
    offset += 4u;
  }

  if (offset < buffer.size()) {
    clock();
    partialMacWord_ = 0u;
    partialBits_ = 32u;
    while (offset < buffer.size()) {
      const unsigned int shift = 32u - partialBits_;
      buffer[offset] ^= static_cast<uint8_t>((streamWord_ >> shift) & 0xffu);
      partialMacWord_ ^= static_cast<uint32_t>(buffer[offset]) << shift;
      ++offset;
      partialBits_ -= 8u;
    }
  }
}

void SpotifyShannon::finish(std::vector<uint8_t>& output) {
  if (partialBits_ != 0u) authenticateWord(partialMacWord_);

  clock();
  state_[KEY_INJECT_WORD] ^= INITIAL_CONSTANT ^ (static_cast<uint32_t>(partialBits_) << 3u);
  partialBits_ = 0u;

  for (size_t i = 0u; i < WORDS; ++i) state_[i] ^= checksum_[i];
  diffuse();

  size_t offset = 0u;
  while (offset < output.size()) {
    clock();
    const size_t count = std::min<size_t>(4u, output.size() - offset);
    for (size_t i = 0u; i < count; ++i) {
      output[offset + i] = static_cast<uint8_t>(streamWord_ >> (8u * i));
    }
    offset += count;
  }
}
