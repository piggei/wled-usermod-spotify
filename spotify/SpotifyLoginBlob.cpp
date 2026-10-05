#include "SpotifyLoginBlob.h"
#include "Aes192Software.h"

#include <LittleFS.h>
#include <esp_system.h>
#include <mbedtls/aes.h>
#include <mbedtls/base64.h>
#include <mbedtls/bignum.h>
#include <mbedtls/sha1.h>

namespace {
constexpr const char* AUTH_PATH = "/spotify_auth.bin";
constexpr uint8_t AUTH_MAGIC[8] = {'S','P','O','T','A','U','T','H'};
constexpr uint8_t AUTH_FILE_VERSION = 1u;

// Spotify/cspot Zeroconf uses the 768-bit RFC 2409 Oakley group 1 with g=2.
constexpr const char* DH_PRIME_HEX =
  "FFFFFFFFFFFFFFFFC90FDAA22168C234"
  "C4C6628B80DC1CD129024E088A67CC74"
  "020BBEA63B139B22514A08798E3404DD"
  "EF9519B3CD3A431B302B0A6DF25F1437"
  "4FE1356D6D51C245E485B576625E7EC6"
  "F44C42E9A63A3620FFFFFFFFFFFFFFFF";

constexpr size_t DH_BYTES = 96u;
constexpr size_t SHA1_BYTES = 20u;
constexpr size_t AES_BLOCK = 16u;
constexpr size_t MAX_AUTH_DATA = 512u;
constexpr size_t MAX_USERNAME = 96u;
}

const char* SpotifyLoginBlob::resultName(Result result) {
  switch (result) {
    case Result::Ok: return "ok";
    case Result::MissingParameter: return "missing parameter";
    case Result::InvalidBase64: return "invalid base64";
    case Result::InvalidClientKey: return "invalid client key";
    case Result::InvalidBlob: return "invalid blob";
    case Result::DhFailure: return "DH failure";
    case Result::MacMismatch: return "MAC mismatch";
    case Result::CryptoFailure: return "crypto failure";
    case Result::SecondaryDecodeFailure: return "secondary decode failure";
    case Result::ParseFailure: return "credential parse failure";
    case Result::PersistFailure: return "credential persist failure";
  }
  return "unknown";
}


const char* SpotifyLoginBlob::stageName(Stage stage) {
  switch (stage) {
    case Stage::Idle: return "idle";
    case Stage::InputValidate: return "input-validate";
    case Stage::Base64Decode: return "base64-decode";
    case Stage::ClientKeyValidate: return "client-key-validate";
    case Stage::BlobValidate: return "blob-validate";
    case Stage::DhSharedKey: return "dh-shared-key";
    case Stage::PrimarySharedHash: return "primary-sha1";
    case Stage::PrimaryChecksumKey: return "primary-checksum-key";
    case Stage::PrimaryEncryptionKey: return "primary-encryption-key";
    case Stage::PrimaryMacVerify: return "primary-mac-verify";
    case Stage::PrimaryAesCtr: return "primary-aes-ctr";
    case Stage::SecondaryBase64: return "secondary-base64";
    case Stage::SecondaryDeviceHash: return "secondary-device-hash";
    case Stage::SecondaryPbkdf2: return "secondary-pbkdf2";
    case Stage::SecondaryBaseHash: return "secondary-base-hash";
    case Stage::SecondaryAesKey: return "secondary-aes-key";
    case Stage::SecondaryAesEcb: return "secondary-aes-ecb";
    case Stage::SecondaryChainXor: return "secondary-chain-xor";
    case Stage::ParseCredential: return "parse-credential";
    case Stage::PersistCredential: return "persist-credential";
    case Stage::Complete: return "complete";
  }
  return "unknown";
}

bool SpotifyLoginBlob::base64Decode(const String& text, std::vector<uint8_t>& out) {
  if (text.length() == 0u) return false;
  const size_t maxDecoded = ((text.length() + 3u) / 4u) * 3u;
  out.assign(maxDecoded, 0u);
  size_t written = 0u;
  const int rc = mbedtls_base64_decode(out.data(), out.size(), &written,
                                       reinterpret_cast<const unsigned char*>(text.c_str()), text.length());
  if (rc != 0) { out.clear(); return false; }
  out.resize(written);
  return true;
}

bool SpotifyLoginBlob::base64Encode(const uint8_t* data, size_t size, char* out, size_t outSize) {
  if (!data || !out || outSize == 0u) return false;
  size_t written = 0u;
  const int rc = mbedtls_base64_encode(reinterpret_cast<unsigned char*>(out), outSize - 1u, &written, data, size);
  if (rc != 0 || written >= outSize) return false;
  out[written] = '\0';
  return true;
}

bool SpotifyLoginBlob::sha1(const uint8_t* data, size_t size, uint8_t out[20]) {
  if (!out || (!data && size != 0u)) return false;

  // Arduino-ESP32 3.x / ESP-IDF 5.5 keeps the streaming SHA-1 context API,
  // while the generic mbedtls_md SHA-1 provider is not available in the
  // qualified WLED build. Use the streaming primitive directly so LoginBlob
  // does not depend on the generic digest registry.
  mbedtls_sha1_context ctx;
  mbedtls_sha1_init(&ctx);
  mbedtls_sha1_starts(&ctx);
  if (size != 0u) mbedtls_sha1_update(&ctx, data, size);
  mbedtls_sha1_finish(&ctx, out);
  mbedtls_sha1_free(&ctx);
  return true;
}

bool SpotifyLoginBlob::hmacSha1(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t dataLen, uint8_t out[20]) {
  if (!out || (!key && keyLen != 0u) || (!data && dataLen != 0u)) return false;

  constexpr size_t SHA1_BLOCK_BYTES = 64u;
  uint8_t normalizedKey[SHA1_BLOCK_BYTES] = {0};
  if (keyLen > SHA1_BLOCK_BYTES) {
    uint8_t keyHash[SHA1_BYTES];
    if (!sha1(key, keyLen, keyHash)) return false;
    memcpy(normalizedKey, keyHash, sizeof(keyHash));
  } else if (keyLen != 0u) {
    memcpy(normalizedKey, key, keyLen);
  }

  uint8_t innerPad[SHA1_BLOCK_BYTES];
  uint8_t outerPad[SHA1_BLOCK_BYTES];
  for (size_t i = 0u; i < SHA1_BLOCK_BYTES; ++i) {
    innerPad[i] = static_cast<uint8_t>(normalizedKey[i] ^ 0x36u);
    outerPad[i] = static_cast<uint8_t>(normalizedKey[i] ^ 0x5cu);
  }

  uint8_t innerDigest[SHA1_BYTES];
  mbedtls_sha1_context ctx;
  mbedtls_sha1_init(&ctx);
  mbedtls_sha1_starts(&ctx);
  mbedtls_sha1_update(&ctx, innerPad, sizeof(innerPad));
  if (dataLen != 0u) mbedtls_sha1_update(&ctx, data, dataLen);
  mbedtls_sha1_finish(&ctx, innerDigest);

  mbedtls_sha1_starts(&ctx);
  mbedtls_sha1_update(&ctx, outerPad, sizeof(outerPad));
  mbedtls_sha1_update(&ctx, innerDigest, sizeof(innerDigest));
  mbedtls_sha1_finish(&ctx, out);
  mbedtls_sha1_free(&ctx);
  return true;
}

bool SpotifyLoginBlob::pbkdf2Sha1(const uint8_t* password, size_t passwordLen,
                                  const uint8_t* salt, size_t saltLen,
                                  uint32_t iterations, uint8_t* out, size_t outLen) {
  if (!password || !salt || !out || iterations == 0u) return false;
  uint32_t blockIndex = 1u;
  size_t produced = 0u;
  std::vector<uint8_t> message(saltLen + 4u);
  memcpy(message.data(), salt, saltLen);
  while (produced < outLen) {
    message[saltLen + 0u] = static_cast<uint8_t>(blockIndex >> 24);
    message[saltLen + 1u] = static_cast<uint8_t>(blockIndex >> 16);
    message[saltLen + 2u] = static_cast<uint8_t>(blockIndex >> 8);
    message[saltLen + 3u] = static_cast<uint8_t>(blockIndex);
    uint8_t u[SHA1_BYTES];
    uint8_t t[SHA1_BYTES];
    if (!hmacSha1(password, passwordLen, message.data(), message.size(), u)) return false;
    memcpy(t, u, sizeof(t));
    for (uint32_t i = 1u; i < iterations; ++i) {
      uint8_t next[SHA1_BYTES];
      if (!hmacSha1(password, passwordLen, u, sizeof(u), next)) return false;
      memcpy(u, next, sizeof(u));
      for (size_t j = 0u; j < sizeof(t); ++j) t[j] ^= u[j];
    }
    const size_t take = min(sizeof(t), outLen - produced);
    memcpy(out + produced, t, take);
    produced += take;
    ++blockIndex;
  }
  return true;
}

bool SpotifyLoginBlob::constantTimeEqual(const uint8_t* a, const uint8_t* b, size_t size) {
  uint8_t diff = 0u;
  for (size_t i = 0u; i < size; ++i) diff |= static_cast<uint8_t>(a[i] ^ b[i]);
  return diff == 0u;
}

bool SpotifyLoginBlob::generateDhKeyPair() {
  esp_fill_random(privateKey_, sizeof(privateKey_));
  privateKey_[0] &= 0x7Fu;
  privateKey_[sizeof(privateKey_) - 1u] |= 0x01u;

  mbedtls_mpi p, g, priv, pub, rr;
  mbedtls_mpi_init(&p); mbedtls_mpi_init(&g); mbedtls_mpi_init(&priv); mbedtls_mpi_init(&pub); mbedtls_mpi_init(&rr);
  bool ok = false;
  do {
    if (mbedtls_mpi_read_string(&p, 16, DH_PRIME_HEX) != 0) break;
    if (mbedtls_mpi_lset(&g, 2) != 0) break;
    if (mbedtls_mpi_read_binary(&priv, privateKey_, sizeof(privateKey_)) != 0) break;
    if (mbedtls_mpi_exp_mod(&pub, &g, &priv, &p, &rr) != 0) break;
    if (mbedtls_mpi_write_binary(&pub, publicKey_, sizeof(publicKey_)) != 0) break;
    if (!base64Encode(publicKey_, sizeof(publicKey_), publicKeyB64_, sizeof(publicKeyB64_))) break;
    ok = true;
  } while (false);
  mbedtls_mpi_free(&rr); mbedtls_mpi_free(&pub); mbedtls_mpi_free(&priv); mbedtls_mpi_free(&g); mbedtls_mpi_free(&p);
  return ok;
}

bool SpotifyLoginBlob::calculateSharedKey(const std::vector<uint8_t>& clientKey, std::vector<uint8_t>& sharedKey) const {
  if (clientKey.size() != DH_BYTES) return false;
  mbedtls_mpi p, peer, priv, shared, rr;
  mbedtls_mpi_init(&p); mbedtls_mpi_init(&peer); mbedtls_mpi_init(&priv); mbedtls_mpi_init(&shared); mbedtls_mpi_init(&rr);
  bool ok = false;
  do {
    if (mbedtls_mpi_read_string(&p, 16, DH_PRIME_HEX) != 0) break;
    if (mbedtls_mpi_read_binary(&peer, clientKey.data(), clientKey.size()) != 0) break;
    if (mbedtls_mpi_cmp_int(&peer, 1) <= 0 || mbedtls_mpi_cmp_mpi(&peer, &p) >= 0) break;
    if (mbedtls_mpi_read_binary(&priv, privateKey_, sizeof(privateKey_)) != 0) break;
    if (mbedtls_mpi_exp_mod(&shared, &peer, &priv, &p, &rr) != 0) break;
    sharedKey.assign(DH_BYTES, 0u);
    if (mbedtls_mpi_write_binary(&shared, sharedKey.data(), sharedKey.size()) != 0) { sharedKey.clear(); break; }
    ok = true;
  } while (false);
  mbedtls_mpi_free(&rr); mbedtls_mpi_free(&shared); mbedtls_mpi_free(&priv); mbedtls_mpi_free(&peer); mbedtls_mpi_free(&p);
  return ok;
}

SpotifyLoginBlob::Result SpotifyLoginBlob::decodePrimary(const std::vector<uint8_t>& blob,
                                                          const std::vector<uint8_t>& sharedKey,
                                                          std::vector<uint8_t>& decoded) const {
  if (blob.size() <= AES_BLOCK + SHA1_BYTES || sharedKey.size() != DH_BYTES) return Result::InvalidBlob;
  diagnosticStage_ = Stage::PrimarySharedHash;
  uint8_t sharedHash[SHA1_BYTES];
  if (!sha1(sharedKey.data(), sharedKey.size(), sharedHash)) return Result::CryptoFailure;

  uint8_t checksumKey[SHA1_BYTES];
  uint8_t encryptionKeyFull[SHA1_BYTES];
  static const uint8_t checksumLabel[] = {'c','h','e','c','k','s','u','m'};
  static const uint8_t encryptionLabel[] = {'e','n','c','r','y','p','t','i','o','n'};
  diagnosticStage_ = Stage::PrimaryChecksumKey;
  if (!hmacSha1(sharedHash, 16u, checksumLabel, sizeof(checksumLabel), checksumKey)) return Result::CryptoFailure;
  diagnosticStage_ = Stage::PrimaryEncryptionKey;
  if (!hmacSha1(sharedHash, 16u, encryptionLabel, sizeof(encryptionLabel), encryptionKeyFull)) return Result::CryptoFailure;

  const size_t encryptedLen = blob.size() - AES_BLOCK - SHA1_BYTES;
  const uint8_t* encrypted = blob.data() + AES_BLOCK;
  const uint8_t* checksum = blob.data() + blob.size() - SHA1_BYTES;
  uint8_t computed[SHA1_BYTES];
  diagnosticStage_ = Stage::PrimaryMacVerify;
  if (!hmacSha1(checksumKey, sizeof(checksumKey), encrypted, encryptedLen, computed)) return Result::CryptoFailure;
  if (!constantTimeEqual(computed, checksum, sizeof(computed))) return Result::MacMismatch;

  decoded.assign(encrypted, encrypted + encryptedLen);
  primaryDecodedBytes_ = decoded.size();
  diagnosticStage_ = Stage::PrimaryAesCtr;
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  if (mbedtls_aes_setkey_enc(&aes, encryptionKeyFull, 128u) != 0) { mbedtls_aes_free(&aes); return Result::CryptoFailure; }
  uint8_t nonceCounter[AES_BLOCK]; memcpy(nonceCounter, blob.data(), AES_BLOCK);
  uint8_t streamBlock[AES_BLOCK] = {0};
  size_t ncOff = 0u;
  const int rc = mbedtls_aes_crypt_ctr(&aes, decoded.size(), &ncOff, nonceCounter, streamBlock,
                                       decoded.data(), decoded.data());
  mbedtls_aes_free(&aes);
  return rc == 0 ? Result::Ok : Result::CryptoFailure;
}

SpotifyLoginBlob::Result SpotifyLoginBlob::decodeSecondary(const std::vector<uint8_t>& decoded,
                                                            const String& userName,
                                                            std::vector<uint8_t>& loginData) const {
  if (decoded.empty() || userName.length() == 0u) return Result::SecondaryDecodeFailure;
  diagnosticStage_ = Stage::SecondaryBase64;
  String innerB64;
  innerB64.reserve(decoded.size() + 1u);
  for (uint8_t c : decoded) innerB64 += static_cast<char>(c);
  if (!base64Decode(innerB64, loginData) || loginData.empty() || (loginData.size() % AES_BLOCK) != 0u)
    return Result::SecondaryDecodeFailure;
  secondaryDecodedBytes_ = loginData.size();

  diagnosticStage_ = Stage::SecondaryDeviceHash;
  uint8_t secret[SHA1_BYTES];
  if (!sha1(reinterpret_cast<const uint8_t*>(deviceId_), strlen(deviceId_), secret)) return Result::CryptoFailure;
  diagnosticStage_ = Stage::SecondaryPbkdf2;
  uint8_t pbk[SHA1_BYTES];
  if (!pbkdf2Sha1(secret, sizeof(secret), reinterpret_cast<const uint8_t*>(userName.c_str()), userName.length(), 256u, pbk, sizeof(pbk)))
    return Result::CryptoFailure;
  diagnosticStage_ = Stage::SecondaryBaseHash;
  uint8_t baseHash[SHA1_BYTES];
  if (!sha1(pbk, sizeof(pbk), baseHash)) return Result::CryptoFailure;
  diagnosticStage_ = Stage::SecondaryAesKey;
  uint8_t aesKey[24];
  memcpy(aesKey, baseHash, sizeof(baseHash));
  aesKey[20] = 0x00; aesKey[21] = 0x00; aesKey[22] = 0x00; aesKey[23] = 0x14;

  diagnosticStage_ = Stage::SecondaryAesEcb;
  // Spotify's secondary LoginBlob layer is AES-192 ECB. ESP32-S3 hardware AES
  // supports AES-128/AES-256 but not AES-192, and the Arduino/IDF mbedTLS port
  // can therefore reject a 192-bit key. Use the small software AES-192 fallback
  // for this one-time pairing operation; the qualified AES-128 CTR primary layer
  // remains on mbedTLS.
  if (!SpotifyCrypto::aes192DecryptEcbInPlace(aesKey, loginData.data(), loginData.size()))
    return Result::CryptoFailure;

  if (loginData.size() < AES_BLOCK) return Result::SecondaryDecodeFailure;
  diagnosticStage_ = Stage::SecondaryChainXor;
  for (size_t i = 0u; i < loginData.size() - AES_BLOCK; ++i) {
    const size_t pos = loginData.size() - i - 1u;
    loginData[pos] ^= loginData[pos - AES_BLOCK];
  }
  return Result::Ok;
}

bool SpotifyLoginBlob::readVarInt(const std::vector<uint8_t>& data, size_t& pos, uint32_t& value) {
  if (pos >= data.size()) return false;
  const uint8_t lo = data[pos++];
  if ((lo & 0x80u) == 0u) { value = lo; return true; }
  if (pos >= data.size()) return false;
  const uint8_t hi = data[pos++];
  value = static_cast<uint32_t>((lo & 0x7Fu) | (static_cast<uint32_t>(hi) << 7));
  return true;
}

SpotifyLoginBlob::Result SpotifyLoginBlob::parseLoginData(const std::vector<uint8_t>& loginData, const String& userName) {
  diagnosticStage_ = Stage::ParseCredential;
  if (loginData.size() < 6u) return Result::ParseFailure;
  size_t pos = 1u;
  uint32_t skip = 0u;
  if (!readVarInt(loginData, pos, skip) || skip > loginData.size() - pos) return Result::ParseFailure;
  pos += skip;
  if (pos >= loginData.size()) return Result::ParseFailure;
  ++pos;
  uint32_t authType = 0u;
  if (!readVarInt(loginData, pos, authType) || authType > 255u) return Result::ParseFailure;
  if (pos >= loginData.size()) return Result::ParseFailure;
  ++pos;
  uint32_t authSize = 0u;
  if (!readVarInt(loginData, pos, authSize) || authSize == 0u || authSize > MAX_AUTH_DATA || authSize > loginData.size() - pos)
    return Result::ParseFailure;

  userName_ = userName;
  authType_ = static_cast<uint8_t>(authType);
  authData_.assign(loginData.begin() + pos, loginData.begin() + pos + authSize);
  credentialsReady_ = true;
  return Result::Ok;
}

bool SpotifyLoginBlob::persist() const {
  if (!credentialsReady_ || userName_.length() > MAX_USERNAME || authData_.size() > MAX_AUTH_DATA) return false;
  File file = LittleFS.open(AUTH_PATH, "w");
  if (!file) return false;
  const uint8_t userLen = static_cast<uint8_t>(userName_.length());
  const uint16_t authLen = static_cast<uint16_t>(authData_.size());
  bool ok = true;
  ok &= file.write(AUTH_MAGIC, sizeof(AUTH_MAGIC)) == sizeof(AUTH_MAGIC);
  ok &= file.write(&AUTH_FILE_VERSION, 1u) == 1u;
  ok &= file.write(&authType_, 1u) == 1u;
  ok &= file.write(&userLen, 1u) == 1u;
  const uint8_t lenBytes[2] = {static_cast<uint8_t>(authLen >> 8), static_cast<uint8_t>(authLen)};
  ok &= file.write(lenBytes, sizeof(lenBytes)) == sizeof(lenBytes);
  ok &= file.write(reinterpret_cast<const uint8_t*>(deviceId_), 40u) == 40u;
  ok &= file.write(reinterpret_cast<const uint8_t*>(userName_.c_str()), userLen) == userLen;
  ok &= file.write(authData_.data(), authData_.size()) == authData_.size();
  file.close();
  return ok;
}

bool SpotifyLoginBlob::loadCached() {
  credentialsReady_ = false;
  userName_ = "";
  authData_.clear();
  File file = LittleFS.open(AUTH_PATH, "r");
  if (!file) { lastError_ = "no cached credentials"; return false; }
  uint8_t magic[sizeof(AUTH_MAGIC)];
  uint8_t version = 0u, authType = 0u, userLen = 0u, lenBytes[2] = {0};
  char fileDeviceId[41] = {0};
  bool ok = file.read(magic, sizeof(magic)) == sizeof(magic) &&
            file.read(&version, 1u) == 1u && file.read(&authType, 1u) == 1u &&
            file.read(&userLen, 1u) == 1u && file.read(lenBytes, sizeof(lenBytes)) == sizeof(lenBytes) &&
            file.read(reinterpret_cast<uint8_t*>(fileDeviceId), 40u) == 40u;
  const uint16_t authLen = static_cast<uint16_t>((static_cast<uint16_t>(lenBytes[0]) << 8) | lenBytes[1]);
  if (!ok || memcmp(magic, AUTH_MAGIC, sizeof(magic)) != 0 || version != AUTH_FILE_VERSION ||
      strcmp(fileDeviceId, deviceId_) != 0 || userLen == 0u || userLen > MAX_USERNAME || authLen == 0u || authLen > MAX_AUTH_DATA) {
    file.close(); lastError_ = "cached credentials invalid"; return false;
  }
  std::vector<uint8_t> user(userLen + 1u, 0u);
  authData_.assign(authLen, 0u);
  if (file.read(user.data(), userLen) != userLen || file.read(authData_.data(), authData_.size()) != authData_.size()) {
    file.close(); authData_.clear(); lastError_ = "cached credentials truncated"; return false;
  }
  file.close();
  userName_ = reinterpret_cast<const char*>(user.data());
  authType_ = authType;
  credentialsReady_ = true;
  lastError_ = "none";
  return true;
}

bool SpotifyLoginBlob::clearCached() {
  credentialsReady_ = false;
  userName_ = "";
  authData_.clear();
  authType_ = 0u;
  if (LittleFS.exists(AUTH_PATH) && !LittleFS.remove(AUTH_PATH)) { lastError_ = "credential remove failed"; return false; }
  lastError_ = "none";
  return true;
}

bool SpotifyLoginBlob::begin(const char* deviceId) {
  diagnosticStage_ = Stage::Idle;
  lastResultValid_ = false;
  if (!deviceId || strlen(deviceId) != 40u) { lastError_ = "invalid device id"; return false; }
  strlcpy(deviceId_, deviceId, sizeof(deviceId_));
  if (!generateDhKeyPair()) { lastError_ = "DH key generation failed"; return false; }
  lastError_ = "none";
  loadCached();
  return true;
}

SpotifyLoginBlob::Result SpotifyLoginBlob::decodeAndStore(const String& userName, const String& blobB64, const String& clientKeyB64) {
  ++decodeAttempts_;

  // Keep the last qualified credential until a replacement has been decoded
  // and (when changed) persisted successfully. This also lets us detect an
  // identical addUser without wearing LittleFS with a redundant rewrite.
  const bool hadCredential = credentialsReady_;
  const String previousUser = userName_;
  const uint8_t previousAuthType = authType_;
  std::vector<uint8_t> previousAuthData = authData_;

  credentialsReady_ = false;
  authType_ = 0u;
  authData_.clear();
  userName_ = "";

  inputUserNameBytes_ = userName.length();
  inputBlobB64Bytes_ = blobB64.length();
  inputClientKeyB64Bytes_ = clientKeyB64.length();
  decodedBlobBytes_ = 0u;
  decodedClientKeyBytes_ = 0u;
  primaryDecodedBytes_ = 0u;
  secondaryDecodedBytes_ = 0u;
  lastResultValid_ = false;

  auto finish = [this, hadCredential, previousUser, previousAuthType, &previousAuthData](Result result) -> Result {
    if (result != Result::Ok) {
      if (hadCredential) {
        userName_ = previousUser;
        authType_ = previousAuthType;
        authData_ = previousAuthData;
        credentialsReady_ = true;
      } else {
        userName_ = "";
        authType_ = 0u;
        authData_.clear();
        credentialsReady_ = false;
      }
    }
    lastResult_ = result;
    lastResultValid_ = true;
    lastError_ = result == Result::Ok ? "none" : resultName(result);
    std::fill(previousAuthData.begin(), previousAuthData.end(), 0u);
    return result;
  };

  diagnosticStage_ = Stage::InputValidate;
  if (userName.length() == 0u || userName.length() > MAX_USERNAME || blobB64.length() == 0u || clientKeyB64.length() == 0u)
    return finish(Result::MissingParameter);

  diagnosticStage_ = Stage::Base64Decode;
  std::vector<uint8_t> blob, clientKey;
  if (!base64Decode(blobB64, blob) || !base64Decode(clientKeyB64, clientKey))
    return finish(Result::InvalidBase64);
  decodedBlobBytes_ = blob.size();
  decodedClientKeyBytes_ = clientKey.size();

  diagnosticStage_ = Stage::ClientKeyValidate;
  if (clientKey.size() != DH_BYTES) return finish(Result::InvalidClientKey);

  diagnosticStage_ = Stage::BlobValidate;
  if (blob.size() <= AES_BLOCK + SHA1_BYTES) return finish(Result::InvalidBlob);

  diagnosticStage_ = Stage::DhSharedKey;
  std::vector<uint8_t> sharedKey;
  if (!calculateSharedKey(clientKey, sharedKey)) return finish(Result::DhFailure);

  std::vector<uint8_t> primary;
  Result result = decodePrimary(blob, sharedKey, primary);
  if (result != Result::Ok) return finish(result);

  std::vector<uint8_t> loginData;
  result = decodeSecondary(primary, userName, loginData);
  if (result != Result::Ok) return finish(result);

  result = parseLoginData(loginData, userName);
  if (result != Result::Ok) return finish(result);
  ++decodeSuccesses_;

  const bool unchanged = hadCredential && previousUser == userName_ &&
                         previousAuthType == authType_ && previousAuthData == authData_;
  diagnosticStage_ = Stage::PersistCredential;
  if (unchanged) {
    ++persistSkips_;
  } else {
    if (!persist()) return finish(Result::PersistFailure);
    ++persistSuccesses_;
  }

  diagnosticStage_ = Stage::Complete;
  return finish(Result::Ok);
}
