#pragma once
#include <Arduino.h>
#include <mbedtls/aes.h>

// Sequential Spotify media AES-128-CTR transform.  The IV is the fixed value
// used by librespot's legacy audio decryptor; callers supply the 16-byte
// AudioKey.  This object intentionally supports only forward sequential bytes
// for the dev.2n qualification path.
class SpotifyAudioAesCtr {
public:
  SpotifyAudioAesCtr();
  ~SpotifyAudioAesCtr();

  bool begin(const uint8_t key[16]);
  bool transform(const uint8_t* input, uint8_t* output, size_t bytes);
  void reset();
  bool ready() const { return ready_; }
  uint64_t position() const { return position_; }

  static constexpr size_t kKeyBytes = 16u;
  static constexpr size_t kIvBytes = 16u;
  static const uint8_t FIXED_IV[kIvBytes];

private:
  mbedtls_aes_context aes_;
  size_t ncOff_ = 0u;
  uint8_t nonceCounter_[kIvBytes]{};
  uint8_t streamBlock_[kIvBytes]{};
  uint64_t position_ = 0u;
  bool ready_ = false;
};
