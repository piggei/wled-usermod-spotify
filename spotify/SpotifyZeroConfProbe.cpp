#include "SpotifyZeroConfProbe.h"
#include "wled.h"
#include <WiFi.h>

namespace {
constexpr uint32_t ADVERTISE_RETRY_MS = 5000u;
constexpr const char* SERVICE = "spotify-connect";
constexpr const char* PROTO = "tcp";
}

String SpotifyZeroConfProbe::jsonEscape(const char* value) {
  String out;
  if (!value) return out;
  out.reserve(strlen(value) + 8u);
  for (const char* p = value; *p; ++p) {
    switch (*p) {
      case '\\': out += F("\\\\"); break;
      case '"': out += F("\\\""); break;
      case '\n': out += F("\\n"); break;
      case '\r': out += F("\\r"); break;
      case '\t': out += F("\\t"); break;
      default:
        if (static_cast<uint8_t>(*p) >= 0x20u) out += *p;
        break;
    }
  }
  return out;
}

void SpotifyZeroConfProbe::ensureIdentity(const char* deviceName) {
  if (deviceId_[0] != '\0' && publicKeyB64_[0] != '\0') return;

  // Stable 40-character development device id.  The first 12 hex digits are
  // the ESP32 eFuse MAC; the remaining digits are deterministic padding for
  // the discovery gate.  dev.2a-r2 will move identity/key ownership into the
  // real LoginBlob implementation.
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(deviceId_, sizeof(deviceId_), "%012llx0000000000000000000000000000",
           static_cast<unsigned long long>(mac & 0xFFFFFFFFFFFFULL));

  // A syntactically valid 96-byte DH public-key field is needed by getInfo.
  // r1 intentionally does not perform addUser/DH, so use the group generator
  // value 2 encoded as a 96-byte big-endian integer.  This is NOT an auth key
  // and must not be reused once LoginBlob support is introduced.
  uint8_t publicKey[96] = {0};
  publicKey[95] = 2u;
  size_t written = 0;
  if (mbedtls_base64_encode(reinterpret_cast<unsigned char*>(publicKeyB64_),
                            sizeof(publicKeyB64_) - 1u, &written,
                            publicKey, sizeof(publicKey)) == 0 &&
      written < sizeof(publicKeyB64_)) {
    publicKeyB64_[written] = '\0';
  } else {
    publicKeyB64_[0] = '\0';
  }

  if (deviceName && *deviceName) strlcpy(deviceName_, deviceName, sizeof(deviceName_));
}

bool SpotifyZeroConfProbe::advertise(const char* deviceName) {
  ++advertiseAttempts_;
  ensureIdentity(deviceName);

  // WLED already owns mDNS lifecycle/hostname.  We only add the Spotify
  // service to that existing responder; never call MDNS.begin() here.
  if (!MDNS.addService(SERVICE, PROTO, httpPort_)) return false;
  MDNS.addServiceTxt(SERVICE, PROTO, "CPath", cpath());
  MDNS.addServiceTxt(SERVICE, PROTO, "VERSION", "1.0");
  advertised_ = true;
  return true;
}

void SpotifyZeroConfProbe::begin(const char* deviceName, uint16_t httpPort) {
  httpPort_ = httpPort;
  ensureIdentity(deviceName);
  if (deviceName && *deviceName) strlcpy(deviceName_, deviceName, sizeof(deviceName_));
  advertised_ = false;
  lastAdvertiseAttemptMs_ = 0;
}

void SpotifyZeroConfProbe::loop(const char* deviceName) {
  if (deviceName && *deviceName && strcmp(deviceName_, deviceName) != 0) strlcpy(deviceName_, deviceName, sizeof(deviceName_));
  if (advertised_ || WiFi.status() != WL_CONNECTED) return;
  const uint32_t now = millis();
  if (lastAdvertiseAttemptMs_ != 0u && now - lastAdvertiseAttemptMs_ < ADVERTISE_RETRY_MS) return;
  lastAdvertiseAttemptMs_ = now;
  advertise(deviceName);
}

String SpotifyZeroConfProbe::buildInfoJson(const char* deviceName) const {
  const String escapedName = jsonEscape(deviceName);
  String out;
  out.reserve(700);
  out += F("{\"status\":101,\"spotifyError\":0,\"statusString\":\"OK\"");
  out += F(",\"version\":\"2.7.1\",\"libraryVersion\":\"cspot-probe-dev2a-r1\"");
  out += F(",\"accountReq\":\"PREMIUM\",\"brandDisplayName\":\"WLED\"");
  out += F(",\"modelDisplayName\":\""); out += escapedName; out += '"';
  out += F(",\"voiceSupport\":\"NO\",\"availability\":\"\",\"productID\":0");
  out += F(",\"tokenType\":\"default\",\"groupStatus\":\"NONE\",\"resolverVersion\":\"0\"");
  out += F(",\"scope\":\"streaming,client-authorization-universal\",\"activeUser\":\"\"");
  out += F(",\"deviceID\":\""); out += deviceId_; out += '"';
  out += F(",\"remoteName\":\""); out += escapedName; out += '"';
  out += F(",\"publicKey\":\""); out += publicKeyB64_; out += '"';
  out += F(",\"deviceType\":\"SPEAKER\"}");
  return out;
}

void SpotifyZeroConfProbe::handleRequest(AsyncWebServerRequest* request) {
  if (!request) return;

  if (request->method() == HTTP_GET) {
    ++getInfoRequests_;
    request->send(200, "application/json", buildInfoJson(deviceName_));
    return;
  }

  if (request->method() == HTTP_POST) {
    ++addUserRequests_;
    ++rejectedAddUserRequests_;

    // Security guardrail for the discovery probe: do not copy, log or persist
    // userName/blob/clientKey.  r2 will add real DH/LoginBlob handling.
    request->send(503, "application/json",
                  F("{\"status\":301,\"spotifyError\":0,\"statusString\":\"ERROR-LOGIN-FAILED\"}"));
    return;
  }

  request->send(405, "application/json",
                F("{\"status\":301,\"spotifyError\":0,\"statusString\":\"ERROR-INVALID-ACTION\"}"));
}

const char* SpotifyZeroConfProbe::stateName() const {
  if (advertised_) return "advertised/getInfo ready; addUser intentionally blocked";
  if (WiFi.status() != WL_CONNECTED) return "waiting for WiFi";
  return "waiting for WLED mDNS responder";
}
