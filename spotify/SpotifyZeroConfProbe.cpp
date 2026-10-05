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

String SpotifyZeroConfProbe::requestParam(AsyncWebServerRequest* request, const char* name) {
  if (!request || !name) return String();
  if (request->hasParam(name, true)) return request->getParam(name, true)->value();
  if (request->hasParam(name)) return request->getParam(name)->value();
  return String();
}

void SpotifyZeroConfProbe::ensureIdentity(const char* deviceName) {
  if (identityReady_) return;

  const uint64_t mac = ESP.getEfuseMac();
  snprintf(deviceId_, sizeof(deviceId_), "%012llx0000000000000000000000000000",
           static_cast<unsigned long long>(mac & 0xFFFFFFFFFFFFULL));
  if (deviceName && *deviceName) strlcpy(deviceName_, deviceName, sizeof(deviceName_));
  identityReady_ = loginBlob_.begin(deviceId_);
}

bool SpotifyZeroConfProbe::advertise(const char* deviceName) {
  ++advertiseAttempts_;
  ensureIdentity(deviceName);
  if (!identityReady_) return false;

  // WLED owns the mDNS lifecycle/hostname. We only attach Spotify's service.
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
  if (deviceName && *deviceName && strcmp(deviceName_, deviceName) != 0)
    strlcpy(deviceName_, deviceName, sizeof(deviceName_));
  if (advertised_ || WiFi.status() != WL_CONNECTED || !identityReady_) return;
  const uint32_t now = millis();
  if (lastAdvertiseAttemptMs_ != 0u && now - lastAdvertiseAttemptMs_ < ADVERTISE_RETRY_MS) return;
  lastAdvertiseAttemptMs_ = now;
  advertise(deviceName);
}

String SpotifyZeroConfProbe::buildInfoJson(const char* deviceName) const {
  const String escapedName = jsonEscape(deviceName);
  String out;
  out.reserve(760);
  out += F("{\"status\":101,\"spotifyError\":0,\"statusString\":\"OK\"");
  out += F(",\"version\":\"2.7.1\",\"libraryVersion\":\"wled-spotify-dev2d\"");
  out += F(",\"accountReq\":\"PREMIUM\",\"brandDisplayName\":\"WLED\"");
  out += F(",\"modelDisplayName\":\""); out += escapedName; out += '"';
  out += F(",\"voiceSupport\":\"NO\",\"availability\":\"");
  out += loginBlob_.credentialsReady() ? F("stored") : F("");
  out += F("\",\"productID\":0,\"tokenType\":\"default\",\"groupStatus\":\"NONE\"");
  out += F(",\"resolverVersion\":\"0\",\"scope\":\"streaming,client-authorization-universal\"");
  out += F(",\"activeUser\":\"\",\"deviceID\":\""); out += deviceId_; out += '"';
  out += F(",\"remoteName\":\""); out += escapedName; out += '"';
  out += F(",\"publicKey\":\""); out += loginBlob_.publicKeyB64(); out += '"';
  out += F(",\"deviceType\":\"SPEAKER\"}");
  return out;
}

void SpotifyZeroConfProbe::handleRequest(AsyncWebServerRequest* request) {
  if (!request) return;

  const String action = requestParam(request, "action");
  if (request->method() == HTTP_GET || action == "getInfo") {
    ++getInfoRequests_;
    request->send(200, "application/json", buildInfoJson(deviceName_));
    return;
  }

  if (request->method() != HTTP_POST) {
    request->send(405, "application/json",
                  F("{\"status\":301,\"spotifyError\":0,\"statusString\":\"ERROR-INVALID-ACTION\"}"));
    return;
  }

  if (action == "resetUsers") {
    ++resetUserRequests_;
    if (loginBlob_.clearCached())
      request->send(200, "application/json", F("{\"status\":101,\"statusString\":\"OK\",\"spotifyError\":0}"));
    else
      request->send(500, "application/json", F("{\"status\":301,\"statusString\":\"ERROR-RESET-USERS\",\"spotifyError\":0}"));
    return;
  }

  if (action != "addUser") {
    request->send(400, "application/json",
                  F("{\"status\":301,\"spotifyError\":0,\"statusString\":\"ERROR-INVALID-ACTION\"}"));
    return;
  }

  ++addUserRequests_;
  lastAddUserUserPresent_ = request->hasParam("userName", true) || request->hasParam("userName");
  lastAddUserBlobPresent_ = request->hasParam("blob", true) || request->hasParam("blob");
  lastAddUserClientKeyPresent_ = request->hasParam("clientKey", true) || request->hasParam("clientKey");
  const String userName = requestParam(request, "userName");
  const String blob = requestParam(request, "blob");
  const String clientKey = requestParam(request, "clientKey");
  const SpotifyLoginBlob::Result result = loginBlob_.decodeAndStore(userName, blob, clientKey);
  if (result == SpotifyLoginBlob::Result::Ok) {
    ++acceptedAddUserRequests_;
    request->send(200, "application/json", F("{\"status\":101,\"statusString\":\"OK\",\"spotifyError\":0}"));
    return;
  }

  ++failedAddUserRequests_;
  if (result == SpotifyLoginBlob::Result::InvalidClientKey) {
    request->send(200, "application/json", F("{\"status\":203,\"statusString\":\"ERROR-INVALID-PUBLICKEY\",\"spotifyError\":0}"));
    return;
  }
  request->send(200, "application/json",
                F("{\"status\":301,\"spotifyError\":0,\"statusString\":\"ERROR-LOGIN-FAILED\"}"));
}

const char* SpotifyZeroConfProbe::stateName() const {
  if (!identityReady_) return "LoginBlob/DH init failed";
  if (advertised_ && loginBlob_.credentialsReady()) return "advertised/getInfo ready; LoginBlob credential cached";
  if (advertised_) return "advertised/getInfo ready; addUser LoginBlob ready";
  if (WiFi.status() != WL_CONNECTED) return "waiting for WiFi";
  return "waiting for WLED mDNS responder";
}
