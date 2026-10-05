#pragma once

#include <stddef.h>
#include <stdint.h>

namespace SpotifyCrypto {

// ESP32-S3 hardware AES acceleration supports AES-128/AES-256, while Spotify's
// secondary LoginBlob layer requires AES-192 ECB. This small software fallback
// is intentionally limited to decrypting whole 16-byte ECB blocks.
bool aes192DecryptEcbInPlace(const uint8_t key[24], uint8_t* data, size_t size);

}  // namespace SpotifyCrypto
