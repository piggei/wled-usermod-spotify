#pragma once

#include <Arduino.h>
#include <ESPmDNS.h>
#include <mbedtls/base64.h>

class AsyncWebServerRequest;

// dev.2a-r1: deliberately narrow Spotify Connect discovery gate.
//
// This class advertises the Spotify Connect mDNS service and exposes the
// current Zeroconf getInfo surface on WLED's existing HTTP server. It does NOT
// decode/store Spotify credentials yet and therefore intentionally rejects
// addUser. Keeping auth out of r1 lets us qualify discovery separately from
// LoginBlob/AP-session work.
class SpotifyZeroConfProbe {
public:
  void begin(const char* deviceName, uint16_t httpPort = 80);
  void loop(const char* deviceName);
  void handleRequest(AsyncWebServerRequest* request);

  bool advertised() const { return advertised_; }
  uint32_t advertiseAttempts() const { return advertiseAttempts_; }
  uint32_t getInfoRequests() const { return getInfoRequests_; }
  uint32_t addUserRequests() const { return addUserRequests_; }
  uint32_t rejectedAddUserRequests() const { return rejectedAddUserRequests_; }
  const char* deviceId() const { return deviceId_; }
  const char* cpath() const { return "/spotify_info"; }
  const char* stateName() const;

private:
  void ensureIdentity(const char* deviceName);
  bool advertise(const char* deviceName);
  String buildInfoJson(const char* deviceName) const;
  static String jsonEscape(const char* value);

  uint16_t httpPort_ = 80;
  bool advertised_ = false;
  uint32_t lastAdvertiseAttemptMs_ = 0;
  uint32_t advertiseAttempts_ = 0;
  uint32_t getInfoRequests_ = 0;
  uint32_t addUserRequests_ = 0;
  uint32_t rejectedAddUserRequests_ = 0;
  char deviceId_[41] = {0};
  char publicKeyB64_[132] = {0};
  char deviceName_[33] = "WLED Matrix";
};
