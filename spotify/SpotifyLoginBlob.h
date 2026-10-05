#pragma once

#include <Arduino.h>
#include <vector>

class SpotifyLoginBlob {
public:
  enum class Result : uint8_t {
    Ok,
    MissingParameter,
    InvalidBase64,
    InvalidClientKey,
    InvalidBlob,
    DhFailure,
    MacMismatch,
    CryptoFailure,
    SecondaryDecodeFailure,
    ParseFailure,
    PersistFailure
  };

  // Diagnostic-only stage tracking. No credential material is exposed.
  enum class Stage : uint8_t {
    Idle,
    InputValidate,
    Base64Decode,
    ClientKeyValidate,
    BlobValidate,
    DhSharedKey,
    PrimarySharedHash,
    PrimaryChecksumKey,
    PrimaryEncryptionKey,
    PrimaryMacVerify,
    PrimaryAesCtr,
    SecondaryBase64,
    SecondaryDeviceHash,
    SecondaryPbkdf2,
    SecondaryBaseHash,
    SecondaryAesKey,
    SecondaryAesEcb,
    SecondaryChainXor,
    ParseCredential,
    PersistCredential,
    Complete
  };

  bool begin(const char* deviceId);
  const char* publicKeyB64() const { return publicKeyB64_; }

  Result decodeAndStore(const String& userName, const String& blobB64, const String& clientKeyB64);
  bool loadCached();
  bool clearCached();

  bool credentialsReady() const { return credentialsReady_; }
  uint8_t authType() const { return authType_; }
  size_t authDataBytes() const { return authData_.size(); }
  size_t userNameBytes() const { return userName_.length(); }
  const char* lastError() const { return lastError_; }
  uint32_t decodeAttempts() const { return decodeAttempts_; }
  uint32_t decodeSuccesses() const { return decodeSuccesses_; }
  uint32_t persistSuccesses() const { return persistSuccesses_; }
  uint32_t persistSkips() const { return persistSkips_; }

  Stage diagnosticStage() const { return diagnosticStage_; }
  const char* diagnosticStageName() const { return stageName(diagnosticStage_); }
  const char* lastResultName() const { return lastResultValid_ ? resultName(lastResult_) : "none"; }
  size_t inputUserNameBytes() const { return inputUserNameBytes_; }
  size_t inputBlobB64Bytes() const { return inputBlobB64Bytes_; }
  size_t inputClientKeyB64Bytes() const { return inputClientKeyB64Bytes_; }
  size_t decodedBlobBytes() const { return decodedBlobBytes_; }
  size_t decodedClientKeyBytes() const { return decodedClientKeyBytes_; }
  size_t primaryDecodedBytes() const { return primaryDecodedBytes_; }
  size_t secondaryDecodedBytes() const { return secondaryDecodedBytes_; }

  // The next milestone can consume these without changing Zeroconf again.
  const String& userName() const { return userName_; }
  const std::vector<uint8_t>& authData() const { return authData_; }

  static const char* resultName(Result result);
  static const char* stageName(Stage stage);

private:
  bool generateDhKeyPair();
  bool calculateSharedKey(const std::vector<uint8_t>& clientKey, std::vector<uint8_t>& sharedKey) const;
  Result decodePrimary(const std::vector<uint8_t>& blob, const std::vector<uint8_t>& sharedKey,
                       std::vector<uint8_t>& decoded) const;
  Result decodeSecondary(const std::vector<uint8_t>& decoded, const String& userName,
                         std::vector<uint8_t>& loginData) const;
  Result parseLoginData(const std::vector<uint8_t>& loginData, const String& userName);
  bool persist() const;

  static bool base64Decode(const String& text, std::vector<uint8_t>& out);
  static bool base64Encode(const uint8_t* data, size_t size, char* out, size_t outSize);
  static bool sha1(const uint8_t* data, size_t size, uint8_t out[20]);
  static bool hmacSha1(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t dataLen, uint8_t out[20]);
  static bool pbkdf2Sha1(const uint8_t* password, size_t passwordLen, const uint8_t* salt, size_t saltLen,
                         uint32_t iterations, uint8_t* out, size_t outLen);
  static bool constantTimeEqual(const uint8_t* a, const uint8_t* b, size_t size);
  static bool readVarInt(const std::vector<uint8_t>& data, size_t& pos, uint32_t& value);

  char deviceId_[41] = {0};
  uint8_t privateKey_[96] = {0};
  uint8_t publicKey_[96] = {0};
  char publicKeyB64_[132] = {0};

  bool credentialsReady_ = false;
  uint8_t authType_ = 0;
  String userName_;
  std::vector<uint8_t> authData_;
  const char* lastError_ = "not initialized";
  uint32_t decodeAttempts_ = 0;
  uint32_t decodeSuccesses_ = 0;
  uint32_t persistSuccesses_ = 0;
  uint32_t persistSkips_ = 0;

  mutable Stage diagnosticStage_ = Stage::Idle;
  Result lastResult_ = Result::Ok;
  bool lastResultValid_ = false;
  size_t inputUserNameBytes_ = 0;
  size_t inputBlobB64Bytes_ = 0;
  size_t inputClientKeyB64Bytes_ = 0;
  size_t decodedBlobBytes_ = 0;
  size_t decodedClientKeyBytes_ = 0;
  mutable size_t primaryDecodedBytes_ = 0;
  mutable size_t secondaryDecodedBytes_ = 0;
};
