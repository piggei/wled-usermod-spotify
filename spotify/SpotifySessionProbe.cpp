#include "SpotifySessionProbe.h"
#include "SpotifyShannon.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <algorithm>
#include <cstring>
#include <esp_system.h>
#include <mbedtls/bignum.h>
#include <mbedtls/sha1.h>

namespace {
constexpr const char* APRESOLVE_HTTP = "http://apresolve.spotify.com/?type=accesspoint";
constexpr const char* AP_FALLBACK = "ap.spotify.com:443";
constexpr const char* KEY_CURRENT = "accesspoint";
constexpr const char* KEY_LEGACY = "ap_list";
constexpr const char* DH_PRIME_HEX =
  "FFFFFFFFFFFFFFFFC90FDAA22168C234"
  "C4C6628B80DC1CD129024E088A67CC74"
  "020BBEA63B139B22514A08798E3404DD"
  "EF9519B3CD3A431B302B0A6DF25F1437"
  "4FE1356D6D51C245E485B576625E7EC6"
  "F44C42E9A63A3620FFFFFFFFFFFFFFFF";
constexpr size_t DH_BYTES = 96u;
constexpr size_t SHA1_BYTES = 20u;
constexpr size_t SHANNON_KEY_BYTES = 32u;
constexpr size_t SHANNON_MAC_BYTES = 4u;
constexpr uint64_t SPOTIFY_VERSION = 0x10800000000ULL;
constexpr uint8_t LOGIN_REQUEST_COMMAND = 0xABu;
constexpr uint8_t AUTH_SUCCESSFUL_COMMAND = 0xACu;
constexpr uint8_t AUTH_DECLINED_COMMAND = 0xADu;
constexpr uint8_t PING_COMMAND = 0x04u;
constexpr uint8_t PONG_COMMAND = 0x49u;
constexpr uint8_t COUNTRY_CODE_COMMAND = 0x1Bu;
constexpr uint8_t MERCURY_SEND_COMMAND = 0xB2u;
constexpr uint8_t MERCURY_SUB_COMMAND = 0xB3u;
constexpr uint8_t MERCURY_UNSUB_COMMAND = 0xB4u;
constexpr uint8_t MERCURY_EVENT_COMMAND = 0xB5u;

bool sha1Digest(const uint8_t* data, size_t size, uint8_t out[SHA1_BYTES]) {
  if (!out || (!data && size != 0u)) return false;
  mbedtls_sha1_context ctx;
  mbedtls_sha1_init(&ctx);
  mbedtls_sha1_starts(&ctx);
  if (size != 0u) mbedtls_sha1_update(&ctx, data, size);
  mbedtls_sha1_finish(&ctx, out);
  mbedtls_sha1_free(&ctx);
  return true;
}

bool hmacSha1(const uint8_t* key, size_t keyLen,
              const uint8_t* data, size_t dataLen,
              uint8_t out[SHA1_BYTES]) {
  if (!out || (!key && keyLen != 0u) || (!data && dataLen != 0u)) return false;
  constexpr size_t BLOCK = 64u;
  uint8_t normalized[BLOCK] = {0};
  if (keyLen > BLOCK) {
    uint8_t digest[SHA1_BYTES];
    if (!sha1Digest(key, keyLen, digest)) return false;
    memcpy(normalized, digest, sizeof(digest));
  } else if (keyLen != 0u) {
    memcpy(normalized, key, keyLen);
  }

  uint8_t innerPad[BLOCK];
  uint8_t outerPad[BLOCK];
  for (size_t i = 0u; i < BLOCK; ++i) {
    innerPad[i] = static_cast<uint8_t>(normalized[i] ^ 0x36u);
    outerPad[i] = static_cast<uint8_t>(normalized[i] ^ 0x5cu);
  }

  uint8_t inner[SHA1_BYTES];
  mbedtls_sha1_context ctx;
  mbedtls_sha1_init(&ctx);
  mbedtls_sha1_starts(&ctx);
  mbedtls_sha1_update(&ctx, innerPad, sizeof(innerPad));
  if (dataLen != 0u) mbedtls_sha1_update(&ctx, data, dataLen);
  mbedtls_sha1_finish(&ctx, inner);
  mbedtls_sha1_starts(&ctx);
  mbedtls_sha1_update(&ctx, outerPad, sizeof(outerPad));
  mbedtls_sha1_update(&ctx, inner, sizeof(inner));
  mbedtls_sha1_finish(&ctx, out);
  mbedtls_sha1_free(&ctx);
  return true;
}

void appendVarint(std::vector<uint8_t>& out, uint64_t value) {
  do {
    uint8_t b = static_cast<uint8_t>(value & 0x7fu);
    value >>= 7u;
    if (value != 0u) b |= 0x80u;
    out.push_back(b);
  } while (value != 0u);
}

void appendKey(std::vector<uint8_t>& out, uint32_t fieldNumber, uint8_t wireType) {
  appendVarint(out, (static_cast<uint64_t>(fieldNumber) << 3u) | wireType);
}

void appendVarintField(std::vector<uint8_t>& out, uint32_t fieldNumber, uint64_t value) {
  appendKey(out, fieldNumber, 0u);
  appendVarint(out, value);
}

void appendBytesField(std::vector<uint8_t>& out, uint32_t fieldNumber,
                      const uint8_t* data, size_t size) {
  appendKey(out, fieldNumber, 2u);
  appendVarint(out, size);
  if (size != 0u) out.insert(out.end(), data, data + size);
}

void appendStringField(std::vector<uint8_t>& out, uint32_t fieldNumber, const String& value) {
  appendBytesField(out, fieldNumber,
                   reinterpret_cast<const uint8_t*>(value.c_str()), value.length());
}

void appendMessageField(std::vector<uint8_t>& out, uint32_t fieldNumber,
                        const std::vector<uint8_t>& message) {
  appendBytesField(out, fieldNumber, message.data(), message.size());
}

bool readVarint(const uint8_t* data, size_t size, size_t& offset, uint64_t& value) {
  value = 0u;
  unsigned int shift = 0u;
  while (offset < size && shift <= 63u) {
    const uint8_t b = data[offset++];
    value |= static_cast<uint64_t>(b & 0x7fu) << shift;
    if ((b & 0x80u) == 0u) return true;
    shift += 7u;
  }
  return false;
}

bool extractLengthDelimited(const uint8_t* data, size_t size, uint32_t wantedField,
                            std::vector<uint8_t>& out) {
  size_t offset = 0u;
  while (offset < size) {
    uint64_t key = 0u;
    if (!readVarint(data, size, offset, key)) return false;
    const uint32_t field = static_cast<uint32_t>(key >> 3u);
    const uint8_t wire = static_cast<uint8_t>(key & 0x07u);

    if (wire == 0u) {
      uint64_t ignored = 0u;
      if (!readVarint(data, size, offset, ignored)) return false;
    } else if (wire == 1u) {
      if (size - offset < 8u) return false;
      offset += 8u;
    } else if (wire == 2u) {
      uint64_t length64 = 0u;
      if (!readVarint(data, size, offset, length64) || length64 > size - offset) return false;
      const size_t length = static_cast<size_t>(length64);
      if (field == wantedField) {
        out.assign(data + offset, data + offset + length);
        return true;
      }
      offset += length;
    } else if (wire == 5u) {
      if (size - offset < 4u) return false;
      offset += 4u;
    } else {
      return false;
    }
  }
  return false;
}

bool parseApDhPublicKey(const std::vector<uint8_t>& apBody, std::vector<uint8_t>& peerKey) {
  std::vector<uint8_t> challenge;
  std::vector<uint8_t> cryptoChallenge;
  std::vector<uint8_t> dhChallenge;
  if (!extractLengthDelimited(apBody.data(), apBody.size(), 10u, challenge)) return false;
  if (!extractLengthDelimited(challenge.data(), challenge.size(), 10u, cryptoChallenge)) return false;
  if (!extractLengthDelimited(cryptoChallenge.data(), cryptoChallenge.size(), 10u, dhChallenge)) return false;
  if (!extractLengthDelimited(dhChallenge.data(), dhChallenge.size(), 10u, peerKey)) return false;
  return peerKey.size() == DH_BYTES;
}

bool generateDhKeyPair(std::vector<uint8_t>& privateKey, std::vector<uint8_t>& publicKey) {
  privateKey.assign(DH_BYTES, 0u);
  publicKey.assign(DH_BYTES, 0u);
  esp_fill_random(privateKey.data(), privateKey.size());

  mbedtls_mpi p, g, priv, pub, rr;
  mbedtls_mpi_init(&p); mbedtls_mpi_init(&g); mbedtls_mpi_init(&priv);
  mbedtls_mpi_init(&pub); mbedtls_mpi_init(&rr);
  bool ok = false;
  do {
    if (mbedtls_mpi_read_string(&p, 16, DH_PRIME_HEX) != 0) break;
    if (mbedtls_mpi_lset(&g, 2) != 0) break;
    if (mbedtls_mpi_read_binary(&priv, privateKey.data(), privateKey.size()) != 0) break;
    if (mbedtls_mpi_exp_mod(&pub, &g, &priv, &p, &rr) != 0) break;
    if (mbedtls_mpi_write_binary(&pub, publicKey.data(), publicKey.size()) != 0) break;
    ok = true;
  } while (false);
  mbedtls_mpi_free(&rr); mbedtls_mpi_free(&pub); mbedtls_mpi_free(&priv);
  mbedtls_mpi_free(&g); mbedtls_mpi_free(&p);
  return ok;
}

bool calculateDhShared(const std::vector<uint8_t>& privateKey,
                       const std::vector<uint8_t>& peerKey,
                       std::vector<uint8_t>& sharedKey) {
  if (privateKey.size() != DH_BYTES || peerKey.size() != DH_BYTES) return false;
  sharedKey.assign(DH_BYTES, 0u);

  mbedtls_mpi p, peer, priv, shared, rr;
  mbedtls_mpi_init(&p); mbedtls_mpi_init(&peer); mbedtls_mpi_init(&priv);
  mbedtls_mpi_init(&shared); mbedtls_mpi_init(&rr);
  bool ok = false;
  do {
    if (mbedtls_mpi_read_string(&p, 16, DH_PRIME_HEX) != 0) break;
    if (mbedtls_mpi_read_binary(&peer, peerKey.data(), peerKey.size()) != 0) break;
    if (mbedtls_mpi_cmp_int(&peer, 1) <= 0 || mbedtls_mpi_cmp_mpi(&peer, &p) >= 0) break;
    if (mbedtls_mpi_read_binary(&priv, privateKey.data(), privateKey.size()) != 0) break;
    if (mbedtls_mpi_exp_mod(&shared, &peer, &priv, &p, &rr) != 0) break;
    if (mbedtls_mpi_write_binary(&shared, sharedKey.data(), sharedKey.size()) != 0) break;
    ok = true;
  } while (false);
  mbedtls_mpi_free(&rr); mbedtls_mpi_free(&shared); mbedtls_mpi_free(&priv);
  mbedtls_mpi_free(&peer); mbedtls_mpi_free(&p);
  if (!ok) sharedKey.clear();
  return ok;
}

std::vector<uint8_t> buildClientHello(const std::vector<uint8_t>& publicKey) {
  std::vector<uint8_t> buildInfo;
  appendVarintField(buildInfo, 10u, 0u);                 // PRODUCT_CLIENT
  appendVarintField(buildInfo, 30u, 2u);                 // PLATFORM_LINUX_X86
  appendVarintField(buildInfo, 40u, SPOTIFY_VERSION);

  std::vector<uint8_t> dhHello;
  appendBytesField(dhHello, 10u, publicKey.data(), publicKey.size());
  appendVarintField(dhHello, 20u, 1u);                   // server_keys_known

  std::vector<uint8_t> cryptoHello;
  appendMessageField(cryptoHello, 10u, dhHello);

  std::vector<uint8_t> featureSet;
  appendVarintField(featureSet, 1u, 1u);                 // autoupdate2

  uint8_t nonce[16];
  esp_fill_random(nonce, sizeof(nonce));
  const uint8_t padding = 0x1eu;

  std::vector<uint8_t> hello;
  // Keep a deterministic field order matching the published message declaration.
  appendMessageField(hello, 10u, buildInfo);
  appendMessageField(hello, 50u, cryptoHello);
  appendVarintField(hello, 30u, 0u);                     // CRYPTO_SUITE_SHANNON
  appendBytesField(hello, 60u, nonce, sizeof(nonce));
  appendBytesField(hello, 70u, &padding, 1u);
  appendMessageField(hello, 80u, featureSet);
  return hello;
}

std::vector<uint8_t> buildClientResponsePlaintext(const uint8_t hmac[SHA1_BYTES]) {
  std::vector<uint8_t> dhResponse;
  appendBytesField(dhResponse, 10u, hmac, SHA1_BYTES);
  std::vector<uint8_t> cryptoResponse;
  appendMessageField(cryptoResponse, 10u, dhResponse);

  std::vector<uint8_t> out;
  appendMessageField(out, 10u, cryptoResponse);
  appendBytesField(out, 20u, nullptr, 0u);               // empty PoWResponseUnion
  appendBytesField(out, 30u, nullptr, 0u);               // empty CryptoResponseUnion
  return out;
}

std::vector<uint8_t> buildAuthRequest(const String& userName, uint8_t authType,
                                      const std::vector<uint8_t>& authData,
                                      const char* deviceId) {
  std::vector<uint8_t> login;
  appendStringField(login, 10u, userName);
  appendVarintField(login, 20u, authType);
  appendBytesField(login, 30u, authData.data(), authData.size());

  std::vector<uint8_t> systemInfo;
  appendVarintField(systemInfo, 10u, 0u);                // CPU_UNKNOWN
  appendVarintField(systemInfo, 60u, 0u);                // OS_UNKNOWN
  appendStringField(systemInfo, 90u, String("wled-spotify"));
  appendStringField(systemInfo, 100u, String(deviceId ? deviceId : ""));

  std::vector<uint8_t> out;
  appendMessageField(out, 10u, login);
  appendMessageField(out, 50u, systemInfo);
  appendStringField(out, 70u, String("wled-spotify-dev2e-r2"));
  return out;
}

void appendBe16(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value >> 8u));
  out.push_back(static_cast<uint8_t>(value));
}

void appendBe32(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value >> 24u));
  out.push_back(static_cast<uint8_t>(value >> 16u));
  out.push_back(static_cast<uint8_t>(value >> 8u));
  out.push_back(static_cast<uint8_t>(value));
}

std::vector<uint8_t> makePlainFrame(const std::vector<uint8_t>& prefix,
                                    const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> packet;
  packet.reserve(prefix.size() + 4u + payload.size());
  packet.insert(packet.end(), prefix.begin(), prefix.end());
  appendBe32(packet, static_cast<uint32_t>(prefix.size() + 4u + payload.size()));
  packet.insert(packet.end(), payload.begin(), payload.end());
  return packet;
}

bool writeAll(WiFiClient& client, const uint8_t* data, size_t size, uint32_t timeoutMs) {
  size_t offset = 0u;
  const uint32_t started = millis();
  while (offset < size) {
    const size_t written = client.write(data + offset, size - offset);
    if (written > 0u) {
      offset += written;
      continue;
    }
    if (millis() - started >= timeoutMs) return false;
    if (!client.connected()) return false;
    delay(1);
  }
  return true;
}

bool readExact(WiFiClient& client, uint8_t* data, size_t size, uint32_t timeoutMs) {
  size_t offset = 0u;
  const uint32_t started = millis();
  while (offset < size) {
    const int available = client.available();
    if (available > 0) {
      const size_t wanted = std::min(size - offset, static_cast<size_t>(available));
      const int got = client.read(data + offset, wanted);
      if (got > 0) {
        offset += static_cast<size_t>(got);
        continue;
      }
    }
    if (millis() - started >= timeoutMs) return false;
    if (!client.connected() && client.available() <= 0) return false;
    delay(1);
  }
  return true;
}

bool readPlainApFrame(WiFiClient& client, size_t maxBytes, uint32_t timeoutMs,
                      std::vector<uint8_t>& fullFrame, std::vector<uint8_t>& body) {
  uint8_t sizeBytes[4];
  if (!readExact(client, sizeBytes, sizeof(sizeBytes), timeoutMs)) return false;
  const uint32_t total = (static_cast<uint32_t>(sizeBytes[0]) << 24u) |
                         (static_cast<uint32_t>(sizeBytes[1]) << 16u) |
                         (static_cast<uint32_t>(sizeBytes[2]) << 8u) |
                         static_cast<uint32_t>(sizeBytes[3]);
  if (total < 4u || total > maxBytes) return false;
  body.assign(total - 4u, 0u);
  if (!body.empty() && !readExact(client, body.data(), body.size(), timeoutMs)) return false;
  fullFrame.assign(sizeBytes, sizeBytes + sizeof(sizeBytes));
  fullFrame.insert(fullFrame.end(), body.begin(), body.end());
  return true;
}

std::vector<uint8_t> shannonNonce(uint32_t value) {
  return {
    static_cast<uint8_t>(value >> 24u),
    static_cast<uint8_t>(value >> 16u),
    static_cast<uint8_t>(value >> 8u),
    static_cast<uint8_t>(value)
  };
}

bool sendShannonPacket(WiFiClient& client, SpotifyShannon& cipher, uint32_t& nonceCounter,
                       uint8_t command, const std::vector<uint8_t>& payload,
                       uint32_t timeoutMs) {
  if (payload.size() > 0xffffu) return false;
  std::vector<uint8_t> raw;
  raw.reserve(3u + payload.size());
  raw.push_back(command);
  appendBe16(raw, static_cast<uint16_t>(payload.size()));
  raw.insert(raw.end(), payload.begin(), payload.end());
  cipher.encrypt(raw);
  std::vector<uint8_t> mac(SHANNON_MAC_BYTES, 0u);
  cipher.finish(mac);
  if (!writeAll(client, raw.data(), raw.size(), timeoutMs) ||
      !writeAll(client, mac.data(), mac.size(), timeoutMs)) return false;
  ++nonceCounter;
  cipher.nonce(shannonNonce(nonceCounter));
  return true;
}

bool recvShannonPacket(WiFiClient& client, SpotifyShannon& cipher, uint32_t& nonceCounter,
                       size_t maxPayload, uint32_t timeoutMs,
                       uint8_t& command, std::vector<uint8_t>& payload, bool& macOk) {
  std::vector<uint8_t> header(3u, 0u);
  if (!readExact(client, header.data(), header.size(), timeoutMs)) return false;
  cipher.decrypt(header);
  command = header[0];
  const size_t payloadSize = (static_cast<size_t>(header[1]) << 8u) | header[2];
  if (payloadSize > maxPayload) return false;

  payload.assign(payloadSize, 0u);
  if (payloadSize != 0u) {
    if (!readExact(client, payload.data(), payload.size(), timeoutMs)) return false;
    cipher.decrypt(payload);
  }

  uint8_t receivedMac[SHANNON_MAC_BYTES];
  if (!readExact(client, receivedMac, sizeof(receivedMac), timeoutMs)) return false;
  std::vector<uint8_t> expectedMac(SHANNON_MAC_BYTES, 0u);
  cipher.finish(expectedMac);
  macOk = memcmp(receivedMac, expectedMac.data(), sizeof(receivedMac)) == 0;
  ++nonceCounter;
  cipher.nonce(shannonNonce(nonceCounter));
  return true;
}

void appendBe64(std::vector<uint8_t>& out, uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8)
    out.push_back(static_cast<uint8_t>(value >> static_cast<unsigned int>(shift)));
}

uint16_t readBe16At(const std::vector<uint8_t>& data, size_t offset) {
  if (offset + 2u > data.size()) return 0u;
  return static_cast<uint16_t>((static_cast<uint16_t>(data[offset]) << 8u) | data[offset + 1u]);
}

uint64_t readBe64At(const std::vector<uint8_t>& data, size_t offset, size_t length) {
  uint64_t value = 0u;
  const size_t take = std::min<size_t>(length, 8u);
  for (size_t i = 0u; i < take && offset + i < data.size(); ++i)
    value = (value << 8u) | data[offset + i];
  return value;
}

bool extractProtoString(const uint8_t* data, size_t size, uint32_t wantedField, String& out) {
  size_t offset = 0u;
  while (offset < size) {
    uint64_t key = 0u;
    if (!readVarint(data, size, offset, key)) return false;
    const uint32_t field = static_cast<uint32_t>(key >> 3u);
    const uint8_t wire = static_cast<uint8_t>(key & 7u);
    if (wire == 2u) {
      uint64_t len = 0u;
      if (!readVarint(data, size, offset, len) || len > size - offset) return false;
      if (field == wantedField) {
        out = String();
        out.reserve(static_cast<unsigned int>(len));
        for (size_t i = 0u; i < static_cast<size_t>(len); ++i)
          out += static_cast<char>(data[offset + i]);
        return true;
      }
      offset += static_cast<size_t>(len);
    } else if (wire == 0u) {
      uint64_t ignored = 0u;
      if (!readVarint(data, size, offset, ignored)) return false;
    } else if (wire == 1u) {
      if (size - offset < 8u) return false;
      offset += 8u;
    } else if (wire == 5u) {
      if (size - offset < 4u) return false;
      offset += 4u;
    } else {
      return false;
    }
  }
  return false;
}

std::vector<uint8_t> buildMercuryRequest(uint64_t sequence, const String& method,
                                         const String& uri) {
  std::vector<uint8_t> header;
  appendStringField(header, 1u, uri);
  appendStringField(header, 3u, method);

  std::vector<uint8_t> out;
  out.reserve(15u + header.size());
  appendBe16(out, 8u);                 // sequence byte count
  appendBe64(out, sequence);
  out.push_back(0x01u);                // final fragment
  appendBe16(out, 1u);                 // header is the only part
  appendBe16(out, static_cast<uint16_t>(header.size()));
  out.insert(out.end(), header.begin(), header.end());
  return out;
}

bool parseMercuryEnvelope(const std::vector<uint8_t>& data, uint64_t& sequence,
                          String& uri, String& method) {
  if (data.size() < 7u) return false;
  const size_t sequenceBytes = readBe16At(data, 0u);
  if (sequenceBytes == 0u || sequenceBytes > 8u || data.size() < 2u + sequenceBytes + 5u)
    return false;
  sequence = readBe64At(data, 2u, sequenceBytes);
  size_t offset = 2u + sequenceBytes;
  offset += 1u; // flags/final-fragment byte
  if (offset + 4u > data.size()) return false;
  const uint16_t partCount = readBe16At(data, offset);
  offset += 2u;
  const uint16_t headerSize = readBe16At(data, offset);
  offset += 2u;
  if (partCount == 0u || headerSize > data.size() - offset) return false;
  const uint8_t* header = data.data() + offset;
  extractProtoString(header, headerSize, 1u, uri);
  extractProtoString(header, headerSize, 3u, method);
  return true;
}
}

void SpotifySessionProbe::begin() {
  state_ = State::Idle;
  task_ = nullptr;
  stopRequested_ = false;
  autoAttempted_ = false;
  eligibleSinceMs_ = 0u;
  taskStartedMs_ = 0u;
  setEndpoint(String());
  setResolverMode("none");
  setError("none");
}

const char* SpotifySessionProbe::stateName() const {
  switch (state_) {
    case State::Idle: return "idle";
    case State::WaitingCredentials: return "waiting-credentials";
    case State::WaitingWifi: return "waiting-wifi";
    case State::Scheduled: return "scheduled";
    case State::Resolving: return "resolving";
    case State::Connecting: return "connecting";
    case State::ClientHello: return "client-hello";
    case State::ApHello: return "ap-hello";
    case State::KeyDerivation: return "key-derivation";
    case State::Authenticating: return "authenticating";
    case State::Authenticated: return "authenticated";
    case State::MercurySubscribing: return "mercury-subscribing";
    case State::SessionActive: return "session-active";
    case State::Reconnecting: return "reconnecting";
    case State::Stopping: return "stopping";
    case State::AuthDeclined: return "auth-declined";
    case State::Failed: return "failed";
  }
  return "unknown";
}

uint32_t SpotifySessionProbe::sessionUptimeMs() const {
  if (sessionConnectedMs_ == 0u) return 0u;
  return millis() - sessionConnectedMs_;
}

uint32_t SpotifySessionProbe::lastRxAgeMs() const {
  if (lastRxMs_ == 0u) return 0u;
  return millis() - lastRxMs_;
}

uint32_t SpotifySessionProbe::lastDurationMs() const {
  if (active() && taskStartedMs_ != 0u) return millis() - taskStartedMs_;
  return lastDurationMs_;
}

void SpotifySessionProbe::setError(const char* text) {
  strlcpy(lastError_, text ? text : "unknown", sizeof(lastError_));
}

void SpotifySessionProbe::setEndpoint(const String& endpoint) {
  strlcpy(endpoint_, endpoint.c_str(), sizeof(endpoint_));
}

void SpotifySessionProbe::setResolverMode(const char* mode) {
  strlcpy(resolverMode_, mode ? mode : "none", sizeof(resolverMode_));
}

void SpotifySessionProbe::updateStackWatermark() {
  const UBaseType_t now = uxTaskGetStackHighWaterMark(nullptr);
  if (stackMinFree_ == 0u || now < stackMinFree_) stackMinFree_ = now;
}

void SpotifySessionProbe::loop(bool enabled, bool credentialsReady,
                               const String& userName, uint8_t authType,
                               const std::vector<uint8_t>& authData, const char* deviceId) {
  if (!enabled) {
    eligibleSinceMs_ = 0u;
    autoAttempted_ = false;
    if (active()) requestStop();
    else state_ = State::Idle;
    return;
  }
  if (!credentialsReady || userName.length() == 0u || authData.empty()) {
    eligibleSinceMs_ = 0u;
    autoAttempted_ = false;
    if (!active()) state_ = State::WaitingCredentials;
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    eligibleSinceMs_ = 0u;
    if (!active()) state_ = State::WaitingWifi;
    return;
  }
  if (active() || autoAttempted_ || state_ == State::SessionActive || state_ == State::MercurySubscribing)
    return;

  const uint32_t now = millis();
  if (eligibleSinceMs_ == 0u) {
    eligibleSinceMs_ = now;
    state_ = State::Scheduled;
    return;
  }
  if (now - eligibleSinceMs_ >= AUTO_DELAY_MS) {
    autoAttempted_ = true;
    startNow(userName, authType, authData, deviceId);
  }
}

bool SpotifySessionProbe::startNow(const String& userName, uint8_t authType,
                                   const std::vector<uint8_t>& authData, const char* deviceId) {
  if (active()) return false;
  if (WiFi.status() != WL_CONNECTED) {
    state_ = State::WaitingWifi;
    setError("WiFi not connected");
    return false;
  }
  if (userName.length() == 0u || authData.empty() || !deviceId || !*deviceId) {
    state_ = State::WaitingCredentials;
    setError("cached credential incomplete");
    return false;
  }

  credentialUser_ = userName;
  credentialAuthType_ = authType;
  credentialAuthData_ = authData;
  strlcpy(credentialDeviceId_, deviceId, sizeof(credentialDeviceId_));

  ++attempts_;
  stopRequested_ = false;
  setError("none");
  setEndpoint(String());
  setResolverMode("none");
  resolveHttpCode_ = 0;
  resolveResponseBytes_ = 0u;
  clientHelloBytes_ = 0u;
  apHelloBytes_ = 0u;
  dhSharedBytes_ = 0u;
  challengeResponseBytes_ = 0u;
  shannonSendKeyBytes_ = 0u;
  shannonRecvKeyBytes_ = 0u;
  authRequestBytes_ = 0u;
  authResponseBytes_ = 0u;
  authLastCommand_ = 0u;
  lastRxCommand_ = 0u;
  sessionConnectedMs_ = 0u;
  lastRxMs_ = 0u;
  heapBefore_ = ESP.getFreeHeap();
  heapAfter_ = heapBefore_;
  lastDurationMs_ = 0u;
  stackMinFree_ = 0u;
  state_ = State::Resolving;
  taskStartedMs_ = millis();

  if (xTaskCreate(taskThunk, "spotify_session", 12288, this, 1, &task_) != pdPASS) {
    task_ = nullptr;
    state_ = State::Failed;
    setError("task create failed");
    std::fill(credentialAuthData_.begin(), credentialAuthData_.end(), 0u);
    credentialAuthData_.clear();
    return false;
  }
  return true;
}

void SpotifySessionProbe::requestStop() {
  if (!active()) return;
  stopRequested_ = true;
  state_ = State::Stopping;
}

void SpotifySessionProbe::reset() {
  if (active()) return;
  stopRequested_ = false;
  autoAttempted_ = false;
  eligibleSinceMs_ = 0u;
  taskStartedMs_ = 0u;
  state_ = State::Idle;
  attempts_ = 0u;
  resolveAttempts_ = 0u;
  resolveSuccesses_ = 0u;
  resolveHttpCode_ = 0;
  resolveResponseBytes_ = 0u;
  fallbackUses_ = 0u;
  tcpAttempts_ = 0u;
  tcpSuccesses_ = 0u;
  handshakeAttempts_ = 0u;
  handshakeSuccesses_ = 0u;
  clientHelloBytes_ = 0u;
  apHelloBytes_ = 0u;
  dhSharedBytes_ = 0u;
  challengeResponseBytes_ = 0u;
  shannonSendKeyBytes_ = 0u;
  shannonRecvKeyBytes_ = 0u;
  authAttempts_ = 0u;
  authSuccesses_ = 0u;
  authDeclines_ = 0u;
  authRequestBytes_ = 0u;
  authResponseBytes_ = 0u;
  authLastCommand_ = 0u;
  shannonMacFailures_ = 0u;
  sessionStarts_ = 0u;
  sessionConnectedMs_ = 0u;
  lastRxMs_ = 0u;
  rxPackets_ = 0u;
  txPackets_ = 0u;
  lastRxCommand_ = 0u;
  pingReceived_ = 0u;
  pongSent_ = 0u;
  serverTimestampSeconds_ = 0u;
  memset(countryCode_, 0, sizeof(countryCode_));
  mercurySequence_ = 0u;
  mercurySubscriptionSequence_ = ~static_cast<uint64_t>(0);
  mercurySubAttempts_ = 0u;
  mercurySubResponses_ = 0u;
  mercuryResponses_ = 0u;
  mercuryEvents_ = 0u;
  mercuryLastSequence_ = 0u;
  memset(mercuryLastUri_, 0, sizeof(mercuryLastUri_));
  reconnectAttempts_ = 0u;
  reconnectSuccesses_ = 0u;
  lastDurationMs_ = 0u;
  heapBefore_ = 0u;
  heapAfter_ = 0u;
  stackMinFree_ = 0u;
  setEndpoint(String());
  setResolverMode("none");
  setError("none");
}

void SpotifySessionProbe::taskThunk(void* arg) {
  static_cast<SpotifySessionProbe*>(arg)->taskLoop();
}

bool SpotifySessionProbe::extractFirstEndpoint(const String& json, const char* key, String& endpoint) {
  if (!key || !*key) return false;
  String needle = String('"') + key + '"';
  int pos = json.indexOf(needle);
  if (pos < 0) return false;
  pos = json.indexOf('[', pos + needle.length());
  if (pos < 0) return false;
  const int start = json.indexOf('"', pos + 1);
  if (start < 0) return false;
  const int end = json.indexOf('"', start + 1);
  if (end <= start + 1) return false;
  endpoint = json.substring(start + 1, end);
  return endpoint.length() > 3u;
}

bool SpotifySessionProbe::splitEndpoint(const String& endpoint, String& host, uint16_t& port) {
  const int colon = endpoint.lastIndexOf(':');
  if (colon <= 0 || colon >= static_cast<int>(endpoint.length()) - 1) return false;
  host = endpoint.substring(0, colon);
  const long parsed = endpoint.substring(colon + 1).toInt();
  if (host.length() == 0u || parsed <= 0 || parsed > 65535) return false;
  port = static_cast<uint16_t>(parsed);
  return true;
}

bool SpotifySessionProbe::resolveWithHttp(String& endpoint) {
  ++resolveAttempts_;
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(IO_TIMEOUT_MS);
  http.setTimeout(IO_TIMEOUT_MS);
  http.setReuse(false);
  if (!http.begin(client, APRESOLVE_HTTP)) {
    setError("HTTP resolver begin failed");
    return false;
  }

  const int code = http.GET();
  resolveHttpCode_ = code;
  String body;
  if (code > 0) body = http.getString();
  resolveResponseBytes_ = body.length();
  http.end();

  if (code != HTTP_CODE_OK) {
    setError("HTTP resolver GET failed");
    return false;
  }
  if (!extractFirstEndpoint(body, KEY_CURRENT, endpoint) &&
      !extractFirstEndpoint(body, KEY_LEGACY, endpoint)) {
    setError("HTTP resolver JSON missing AP");
    return false;
  }
  ++resolveSuccesses_;
  setResolverMode("http");
  return true;
}

bool SpotifySessionProbe::resolveAccessPoint(String& endpoint) {
  if (resolveWithHttp(endpoint)) return true;
  endpoint = AP_FALLBACK;
  ++fallbackUses_;
  setResolverMode("fallback");
  return true;
}

void SpotifySessionProbe::finishTask(uint32_t startedMs) {
  lastDurationMs_ = millis() - startedMs;
  heapAfter_ = ESP.getFreeHeap();
  updateStackWatermark();
  std::fill(credentialAuthData_.begin(), credentialAuthData_.end(), 0u);
  credentialAuthData_.clear();
  credentialUser_ = String();
  credentialAuthType_ = 0u;
  memset(credentialDeviceId_, 0, sizeof(credentialDeviceId_));
  taskStartedMs_ = 0u;
  task_ = nullptr;
}

bool SpotifySessionProbe::runOneSession(bool reconnecting) {
  String endpoint;
  WiFiClient tcp;

  state_ = State::Resolving;
  if (!resolveAccessPoint(endpoint)) { state_ = State::Failed; return false; }
  setEndpoint(endpoint);

  String host;
  uint16_t port = 0u;
  if (!splitEndpoint(endpoint, host, port)) {
    state_ = State::Failed; setError("invalid AP endpoint"); return false;
  }

  state_ = State::Connecting;
  ++tcpAttempts_;
  if (tcp.connect(host.c_str(), port, CONNECT_TIMEOUT_MS) != 1) {
    tcp.stop(); state_ = State::Failed; setError("AP TCP connect failed"); return false;
  }
  ++tcpSuccesses_;

  ++handshakeAttempts_;
  state_ = State::ClientHello;
  std::vector<uint8_t> privateKey;
  std::vector<uint8_t> publicKey;
  if (!generateDhKeyPair(privateKey, publicKey)) {
    tcp.stop(); state_ = State::Failed; setError("AP DH key generation failed"); return false;
  }

  const std::vector<uint8_t> helloProto = buildClientHello(publicKey);
  const std::vector<uint8_t> helloPacket = makePlainFrame({0x00u, 0x04u}, helloProto);
  clientHelloBytes_ = helloPacket.size();
  if (!writeAll(tcp, helloPacket.data(), helloPacket.size(), IO_TIMEOUT_MS)) {
    tcp.stop(); state_ = State::Failed; setError("ClientHello write failed"); return false;
  }

  state_ = State::ApHello;
  std::vector<uint8_t> apFrame;
  std::vector<uint8_t> apBody;
  if (!readPlainApFrame(tcp, MAX_AP_PLAIN_PACKET, IO_TIMEOUT_MS, apFrame, apBody)) {
    tcp.stop(); state_ = State::Failed; setError("APResponse read failed"); return false;
  }
  apHelloBytes_ = apFrame.size();

  std::vector<uint8_t> peerKey;
  if (!parseApDhPublicKey(apBody, peerKey)) {
    tcp.stop(); state_ = State::Failed; setError("APResponse DH key missing"); return false;
  }

  state_ = State::KeyDerivation;
  std::vector<uint8_t> sharedKey;
  if (!calculateDhShared(privateKey, peerKey, sharedKey)) {
    tcp.stop(); state_ = State::Failed; setError("AP DH shared key failed"); return false;
  }
  std::fill(privateKey.begin(), privateKey.end(), 0u);
  privateKey.clear();
  dhSharedBytes_ = sharedKey.size();

  std::vector<uint8_t> challengeData;
  challengeData.reserve(helloPacket.size() + apFrame.size() + 1u);
  challengeData.insert(challengeData.end(), helloPacket.begin(), helloPacket.end());
  challengeData.insert(challengeData.end(), apFrame.begin(), apFrame.end());

  std::vector<uint8_t> resultData;
  resultData.reserve(100u);
  for (uint8_t counter = 1u; counter <= 5u; ++counter) {
    std::vector<uint8_t> one = challengeData;
    one.push_back(counter);
    uint8_t digest[SHA1_BYTES];
    if (!hmacSha1(sharedKey.data(), sharedKey.size(), one.data(), one.size(), digest)) {
      tcp.stop(); state_ = State::Failed; setError("AP challenge HMAC failed"); return false;
    }
    resultData.insert(resultData.end(), digest, digest + sizeof(digest));
  }
  std::fill(sharedKey.begin(), sharedKey.end(), 0u);
  sharedKey.clear();

  if (resultData.size() < 84u) {
    tcp.stop(); state_ = State::Failed; setError("AP challenge key material short"); return false;
  }

  uint8_t challengeHmac[SHA1_BYTES];
  if (!hmacSha1(resultData.data(), SHA1_BYTES, challengeData.data(), challengeData.size(), challengeHmac)) {
    tcp.stop(); state_ = State::Failed; setError("AP response HMAC failed"); return false;
  }

  std::vector<uint8_t> sendKey(resultData.begin() + 20, resultData.begin() + 52);
  std::vector<uint8_t> recvKey(resultData.begin() + 52, resultData.begin() + 84);
  shannonSendKeyBytes_ = sendKey.size();
  shannonRecvKeyBytes_ = recvKey.size();
  std::fill(resultData.begin(), resultData.end(), 0u);
  resultData.clear();

  const std::vector<uint8_t> challengeResponse = buildClientResponsePlaintext(challengeHmac);
  challengeResponseBytes_ = challengeResponse.size();
  const std::vector<uint8_t> responseFrame = makePlainFrame({}, challengeResponse);
  if (!writeAll(tcp, responseFrame.data(), responseFrame.size(), IO_TIMEOUT_MS)) {
    tcp.stop(); state_ = State::Failed; setError("ClientResponsePlaintext write failed"); return false;
  }
  ++handshakeSuccesses_;

  SpotifyShannon sendCipher;
  SpotifyShannon recvCipher;
  sendCipher.key(sendKey);
  recvCipher.key(recvKey);
  uint32_t sendNonce = 0u;
  uint32_t recvNonce = 0u;
  sendCipher.nonce(shannonNonce(sendNonce));
  recvCipher.nonce(shannonNonce(recvNonce));
  std::fill(sendKey.begin(), sendKey.end(), 0u);
  std::fill(recvKey.begin(), recvKey.end(), 0u);

  state_ = State::Authenticating;
  ++authAttempts_;
  std::vector<uint8_t> authRequest = buildAuthRequest(
      credentialUser_, credentialAuthType_, credentialAuthData_, credentialDeviceId_);
  authRequestBytes_ = authRequest.size();
  if (!sendShannonPacket(tcp, sendCipher, sendNonce, LOGIN_REQUEST_COMMAND, authRequest, IO_TIMEOUT_MS)) {
    std::fill(authRequest.begin(), authRequest.end(), 0u);
    tcp.stop(); state_ = State::Failed; setError("Shannon login write failed"); return false;
  }
  std::fill(authRequest.begin(), authRequest.end(), 0u);
  authRequest.clear();

  std::vector<uint8_t> authResponse;
  bool macOk = false;
  uint8_t command = 0u;
  if (!recvShannonPacket(tcp, recvCipher, recvNonce, MAX_AP_ENCRYPTED_PACKET,
                         IO_TIMEOUT_MS, command, authResponse, macOk)) {
    tcp.stop(); state_ = State::Failed; setError("Shannon auth response read failed"); return false;
  }
  authLastCommand_ = command;
  authResponseBytes_ = authResponse.size();
  if (!macOk) {
    ++shannonMacFailures_;
    tcp.stop(); state_ = State::Failed; setError("Shannon MAC mismatch"); return false;
  }
  if (command == AUTH_DECLINED_COMMAND) {
    ++authDeclines_;
    tcp.stop(); state_ = State::AuthDeclined; setError("AP authorization declined"); return false;
  }
  if (command != AUTH_SUCCESSFUL_COMMAND) {
    tcp.stop(); state_ = State::Failed; setError("unexpected AP auth command"); return false;
  }

  ++authSuccesses_;
  ++sessionStarts_;
  if (reconnecting) ++reconnectSuccesses_;
  state_ = State::Authenticated;
  setError("none");
  sessionConnectedMs_ = millis();
  lastRxMs_ = sessionConnectedMs_;
  updateStackWatermark();

  // Minimal Mercury gate. Current embedded cspot interoperability references use
  // hm://remote/3/user/<user>/.  We wait for Spotify's first authenticated PING
  // before sending SUB, mirroring the observed AP startup ordering: APWelcome,
  // PING/PONG time sync, then Mercury subscription.
  const String subscriptionUri = String(F("hm://remote/3/user/")) + credentialUser_ + F("/");
  bool subscriptionSent = false;

  while (!stopRequested_) {
    if (WiFi.status() != WL_CONNECTED) {
      tcp.stop(); state_ = State::Failed; setError("WiFi lost during Spotify session"); return false;
    }

    if (tcp.available() <= 0) {
      if (!tcp.connected()) {
        tcp.stop(); state_ = State::Failed; setError("Spotify AP closed session"); return false;
      }
      if (lastRxMs_ != 0u && millis() - lastRxMs_ > SESSION_RX_TIMEOUT_MS) {
        tcp.stop(); state_ = State::Failed; setError("Spotify session RX timeout"); return false;
      }
      updateStackWatermark();
      delay(SESSION_POLL_MS);
      continue;
    }

    std::vector<uint8_t> payload;
    bool liveMacOk = false;
    uint8_t liveCommand = 0u;
    if (!recvShannonPacket(tcp, recvCipher, recvNonce, MAX_AP_ENCRYPTED_PACKET,
                           IO_TIMEOUT_MS, liveCommand, payload, liveMacOk)) {
      tcp.stop(); state_ = State::Failed; setError("Spotify session packet read failed"); return false;
    }
    ++rxPackets_;
    lastRxCommand_ = liveCommand;
    lastRxMs_ = millis();
    if (!liveMacOk) {
      ++shannonMacFailures_;
      tcp.stop(); state_ = State::Failed; setError("Shannon live MAC mismatch"); return false;
    }

    if (liveCommand == PING_COMMAND) {
      ++pingReceived_;
      if (payload.size() >= 4u) {
        serverTimestampSeconds_ = (static_cast<uint32_t>(payload[0]) << 24u) |
                                  (static_cast<uint32_t>(payload[1]) << 16u) |
                                  (static_cast<uint32_t>(payload[2]) << 8u) |
                                  static_cast<uint32_t>(payload[3]);
      }
      if (!sendShannonPacket(tcp, sendCipher, sendNonce, PONG_COMMAND, payload, IO_TIMEOUT_MS)) {
        tcp.stop(); state_ = State::Failed; setError("Spotify PONG write failed"); return false;
      }
      ++pongSent_;
      ++txPackets_;

      if (!subscriptionSent) {
        state_ = State::MercurySubscribing;
        mercurySubscriptionSequence_ = mercurySequence_++;
        const std::vector<uint8_t> subRequest = buildMercuryRequest(
            mercurySubscriptionSequence_, String(F("SUB")), subscriptionUri);
        ++mercurySubAttempts_;
        if (!sendShannonPacket(tcp, sendCipher, sendNonce, MERCURY_SUB_COMMAND,
                               subRequest, IO_TIMEOUT_MS)) {
          tcp.stop(); state_ = State::Failed; setError("Mercury SUB write failed"); return false;
        }
        subscriptionSent = true;
        ++txPackets_;
      }

      if (mercurySubResponses_ > 0u) state_ = State::SessionActive;
      continue;
    }

    if (liveCommand == COUNTRY_CODE_COMMAND && payload.size() >= 2u) {
      countryCode_[0] = static_cast<char>(payload[0]);
      countryCode_[1] = static_cast<char>(payload[1]);
      countryCode_[2] = '\0';
      continue;
    }

    if (liveCommand == MERCURY_SEND_COMMAND || liveCommand == MERCURY_SUB_COMMAND ||
        liveCommand == MERCURY_UNSUB_COMMAND || liveCommand == MERCURY_EVENT_COMMAND) {
      uint64_t sequence = 0u;
      String uri, method;
      if (parseMercuryEnvelope(payload, sequence, uri, method)) {
        mercuryLastSequence_ = sequence;
        if (uri.length() != 0u) strlcpy(mercuryLastUri_, uri.c_str(), sizeof(mercuryLastUri_));
        if (liveCommand == MERCURY_EVENT_COMMAND) {
          ++mercuryEvents_;
          // Spotify delivers subscription traffic on 0xB5. Treat the first
          // event for our remote-user URI as proof that the SUB is active.
          if (subscriptionSent && uri == subscriptionUri) {
            ++mercurySubResponses_;
            state_ = State::SessionActive;
          }
        } else {
          ++mercuryResponses_;
          // Some AP implementations can acknowledge the SUB directly on 0xB3.
          if (liveCommand == MERCURY_SUB_COMMAND && sequence == mercurySubscriptionSequence_) {
            ++mercurySubResponses_;
            state_ = State::SessionActive;
          }
        }
      }
      continue;
    }
  }

  tcp.stop();
  state_ = State::Stopping;
  setError("none");
  return true;
}

void SpotifySessionProbe::taskLoop() {
  const uint32_t started = millis();
  uint32_t reconnectsUsed = 0u;
  bool reconnecting = false;

  while (!stopRequested_) {
    if (reconnecting) {
      state_ = State::Reconnecting;

      // A Wi-Fi outage must not burn through the bounded AP reconnect budget.
      // Wait for WLED networking to recover first, then count an AP attempt.
      while (!stopRequested_ && WiFi.status() != WL_CONNECTED) {
        updateStackWatermark();
        delay(250);
      }
      if (stopRequested_) break;

      if (reconnectsUsed >= MAX_AUTO_RECONNECTS) {
        state_ = State::Failed;
        setError("Spotify reconnect limit reached");
        break;
      }
      ++reconnectAttempts_;
      ++reconnectsUsed;
      const uint32_t waitStart = millis();
      while (!stopRequested_ && millis() - waitStart < RECONNECT_DELAY_MS) delay(25);
      if (stopRequested_) break;
    }

    const bool graceful = runOneSession(reconnecting);
    if (stopRequested_ || graceful) break;
    reconnecting = true;
  }

  if (stopRequested_) {
    state_ = State::Idle;
    setError("none");
  }
  finishTask(started);
  vTaskDelete(nullptr);
}
