#include "SpotifyAudioAesCtr.h"
#include <cstring>

const uint8_t SpotifyAudioAesCtr::FIXED_IV[SpotifyAudioAesCtr::kIvBytes] = {
    0x72, 0xe0, 0x67, 0xfb, 0xdd, 0xcb, 0xcf, 0x77,
    0xeb, 0xe8, 0xbc, 0x64, 0x3f, 0x63, 0x0d, 0x93,
};

SpotifyAudioAesCtr::SpotifyAudioAesCtr() {
  mbedtls_aes_init(&aes_);
}

SpotifyAudioAesCtr::~SpotifyAudioAesCtr() {
  mbedtls_aes_free(&aes_);
}

bool SpotifyAudioAesCtr::begin(const uint8_t key[16]) {
  reset();
  if (!key) return false;
  if (mbedtls_aes_setkey_enc(&aes_, key, 128u) != 0) return false;
  memcpy(nonceCounter_, FIXED_IV, sizeof(nonceCounter_));
  ready_ = true;
  return true;
}

bool SpotifyAudioAesCtr::transform(const uint8_t* input, uint8_t* output, size_t bytes) {
  if (!ready_ || (!input && bytes != 0u) || (!output && bytes != 0u)) return false;
  if (bytes == 0u) return true;
  const int rc = mbedtls_aes_crypt_ctr(&aes_, bytes, &ncOff_, nonceCounter_, streamBlock_, input, output);
  if (rc != 0) return false;
  position_ += static_cast<uint64_t>(bytes);
  return true;
}

void SpotifyAudioAesCtr::reset() {
  ncOff_ = 0u;
  memset(nonceCounter_, 0, sizeof(nonceCounter_));
  memset(streamBlock_, 0, sizeof(streamBlock_));
  position_ = 0u;
  ready_ = false;
}
