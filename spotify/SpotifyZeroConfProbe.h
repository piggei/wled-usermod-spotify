#pragma once

#include <Arduino.h>
#include <ESPmDNS.h>
#include "SpotifyLoginBlob.h"

class AsyncWebServerRequest;

// dev.2d-r3: target-compatible SHA1/HMAC backend on top of the r2 diagnostics.
//
// Discovery/getInfo stays on WLED's HTTP + mDNS services. POST addUser now
// performs the cspot-compatible 96-byte DH exchange, validates/decrypts the
// LoginBlob and stores only the reusable Spotify credential in LittleFS.
// No AP/Shannon session is started in this gate: authentication is qualified
// independently before the network/session runtime is introduced.
class SpotifyZeroConfProbe {
public:
  void begin(const char* deviceName, uint16_t httpPort = 80);
  void loop(const char* deviceName);
  void handleRequest(AsyncWebServerRequest* request);

  bool advertised() const { return advertised_; }
  uint32_t advertiseAttempts() const { return advertiseAttempts_; }
  uint32_t getInfoRequests() const { return getInfoRequests_; }
  uint32_t addUserRequests() const { return addUserRequests_; }
  uint32_t acceptedAddUserRequests() const { return acceptedAddUserRequests_; }
  uint32_t failedAddUserRequests() const { return failedAddUserRequests_; }
  uint32_t resetUserRequests() const { return resetUserRequests_; }
  const char* deviceId() const { return deviceId_; }
  const char* cpath() const { return "/spotify_info"; }
  const char* stateName() const;

  bool credentialsReady() const { return loginBlob_.credentialsReady(); }
  const String& userName() const { return loginBlob_.userName(); }
  const std::vector<uint8_t>& authData() const { return loginBlob_.authData(); }
  uint8_t authType() const { return loginBlob_.authType(); }
  size_t authDataBytes() const { return loginBlob_.authDataBytes(); }
  size_t userNameBytes() const { return loginBlob_.userNameBytes(); }
  const char* authError() const { return loginBlob_.lastError(); }
  uint32_t authDecodeAttempts() const { return loginBlob_.decodeAttempts(); }
  uint32_t authDecodeSuccesses() const { return loginBlob_.decodeSuccesses(); }
  uint32_t authPersistSuccesses() const { return loginBlob_.persistSuccesses(); }
  uint32_t authPersistSkips() const { return loginBlob_.persistSkips(); }
  const char* authStage() const { return loginBlob_.diagnosticStageName(); }
  const char* authLastResult() const { return loginBlob_.lastResultName(); }
  size_t authInputUserBytes() const { return loginBlob_.inputUserNameBytes(); }
  size_t authInputBlobB64Bytes() const { return loginBlob_.inputBlobB64Bytes(); }
  size_t authInputClientKeyB64Bytes() const { return loginBlob_.inputClientKeyB64Bytes(); }
  size_t authDecodedBlobBytes() const { return loginBlob_.decodedBlobBytes(); }
  size_t authDecodedClientKeyBytes() const { return loginBlob_.decodedClientKeyBytes(); }
  size_t authPrimaryBytes() const { return loginBlob_.primaryDecodedBytes(); }
  size_t authSecondaryBytes() const { return loginBlob_.secondaryDecodedBytes(); }

  bool lastAddUserUserPresent() const { return lastAddUserUserPresent_; }
  bool lastAddUserBlobPresent() const { return lastAddUserBlobPresent_; }
  bool lastAddUserClientKeyPresent() const { return lastAddUserClientKeyPresent_; }

private:
  void ensureIdentity(const char* deviceName);
  bool advertise(const char* deviceName);
  String buildInfoJson(const char* deviceName) const;
  static String jsonEscape(const char* value);
  static String requestParam(AsyncWebServerRequest* request, const char* name);

  uint16_t httpPort_ = 80;
  bool advertised_ = false;
  bool identityReady_ = false;
  uint32_t lastAdvertiseAttemptMs_ = 0;
  uint32_t advertiseAttempts_ = 0;
  uint32_t getInfoRequests_ = 0;
  uint32_t addUserRequests_ = 0;
  uint32_t acceptedAddUserRequests_ = 0;
  uint32_t failedAddUserRequests_ = 0;
  uint32_t resetUserRequests_ = 0;
  bool lastAddUserUserPresent_ = false;
  bool lastAddUserBlobPresent_ = false;
  bool lastAddUserClientKeyPresent_ = false;
  char deviceId_[41] = {0};
  char deviceName_[33] = "WLED Matrix";
  SpotifyLoginBlob loginBlob_;
};
