#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Independent C++ implementation of the Shannon stream cipher primitive used
// by Spotify AP packet transport. The algorithm is implemented from the public
// Shannon design/specification; no cspot source is bundled here. The host-side
// regression vector is cross-checked against the public twonky4/shannon vector.
// See THIRD_PARTY_NOTICES.md.
class SpotifyShannon {
public:
  void key(const std::vector<uint8_t>& bytes);
  void nonce(const std::vector<uint8_t>& bytes);
  void stream(std::vector<uint8_t>& buffer);
  void macOnly(std::vector<uint8_t>& buffer);
  void encrypt(std::vector<uint8_t>& buffer);
  void decrypt(std::vector<uint8_t>& buffer);
  void finish(std::vector<uint8_t>& output);

private:
  static constexpr size_t WORDS = 16u;
  static constexpr size_t KEY_INJECT_WORD = 13u;
  static constexpr uint32_t INITIAL_CONSTANT = 0x6996c53au;

  uint32_t state_[WORDS] = {0};
  uint32_t checksum_[WORDS] = {0};
  uint32_t keyedState_[WORDS] = {0};
  uint32_t constant_ = INITIAL_CONSTANT;
  uint32_t streamWord_ = 0u;
  uint32_t partialMacWord_ = 0u;
  unsigned int partialBits_ = 0u;

  static uint32_t rotateLeft(uint32_t value, unsigned int count);
  static uint32_t nonlinearA(uint32_t value);
  static uint32_t nonlinearB(uint32_t value);
  static uint32_t loadLe32(const uint8_t* bytes);
  static void storeLe32(uint32_t value, uint8_t* bytes);

  void clock();
  void checksumWord(uint32_t word);
  void authenticateWord(uint32_t word);
  void seedFibonacci();
  void diffuse();
  void absorb(const uint8_t* bytes, size_t length);
  void rememberKeyedState();
  void restoreKeyedState();
  void clearPartial();
};
