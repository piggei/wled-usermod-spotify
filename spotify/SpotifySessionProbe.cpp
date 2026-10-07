#include "SpotifySessionProbe.h"
#include "SpotifyShannon.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <utility>
#include <esp_system.h>
#if __has_include(<miniz.h>)
#include <miniz.h>
#define SPOTIFY_HAVE_ROM_MINIZ 1
#else
#define SPOTIFY_HAVE_ROM_MINIZ 0
#endif
#if __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#define SPOTIFY_HAVE_HEAP_CAPS 1
#else
#define SPOTIFY_HAVE_HEAP_CAPS 0
#endif
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
constexpr uint8_t CLIENT_PRODUCT_CLASS = 0u;      // PRODUCT_CLIENT
constexpr uint8_t CLIENT_PLATFORM_CLASS = 2u;     // PLATFORM_LINUX_X86
constexpr uint8_t AUTH_CPU_CLASS = 0u;            // CPU_UNKNOWN
constexpr uint8_t AUTH_OS_CLASS = 0u;             // OS_UNKNOWN
constexpr const char* AUTH_SYSTEM_NAME = "wled-spotify";
constexpr const char* AUTH_CLIENT_VERSION = "wled-spotify-dev2e-r2";
constexpr uint8_t LOGIN_REQUEST_COMMAND = 0xABu;
constexpr uint8_t AUTH_SUCCESSFUL_COMMAND = 0xACu;
constexpr uint8_t AUTH_DECLINED_COMMAND = 0xADu;
constexpr uint8_t PING_COMMAND = 0x04u;
constexpr uint8_t PONG_COMMAND = 0x49u;
constexpr uint8_t COUNTRY_CODE_COMMAND = 0x1Bu;
constexpr uint8_t PRODUCT_INFO_COMMAND = 0x50u;
constexpr uint8_t STREAM_CHUNK_REQUEST_COMMAND = 0x08u;
constexpr uint8_t STREAM_CHUNK_SUCCESS_COMMAND = 0x09u;
constexpr uint8_t STREAM_CHUNK_FAILURE_COMMAND = 0x0Au;
constexpr uint8_t REQUEST_KEY_COMMAND = 0x0Cu;
constexpr uint8_t AES_KEY_COMMAND = 0x0Du;
constexpr uint8_t AES_KEY_ERROR_COMMAND = 0x0Eu;
constexpr size_t TRACK_GID_BYTES = 16u;
constexpr size_t AUDIO_FILE_ID_BYTES = 20u;
constexpr size_t AUDIO_AES_KEY_BYTES = 16u;
constexpr uint8_t MERCURY_SEND_COMMAND = 0xB2u;
constexpr uint8_t MERCURY_SUB_COMMAND = 0xB3u;
constexpr uint8_t MERCURY_UNSUB_COMMAND = 0xB4u;
constexpr uint8_t MERCURY_EVENT_COMMAND = 0xB5u;
constexpr uint32_t SPIRC_HELLO = 0x01u;
constexpr uint32_t SPIRC_NOTIFY = 0x0Au;
constexpr uint32_t SPIRC_LOAD = 0x14u;
constexpr uint32_t SPIRC_PLAY = 0x15u;
constexpr uint32_t SPIRC_PAUSE = 0x16u;
constexpr uint32_t SPIRC_PLAY_PAUSE = 0x17u;
constexpr uint32_t SPIRC_SEEK = 0x18u;
constexpr uint32_t SPIRC_PREV = 0x19u;
constexpr uint32_t SPIRC_NEXT = 0x1Au;
constexpr uint32_t SPIRC_REPLACE = 0x21u;
constexpr const char* SPIRC_PROTOCOL_VERSION = "2.7.1";
constexpr size_t MAX_SPIRC_STATE_TRACK_REFS = 96u;
constexpr size_t MAX_SPIRC_STATE_TRACK_REF_BYTES = 12288u;
constexpr size_t MAX_CONTEXT_PLAYER_INFLATED_BYTES = 131072u;
constexpr const char* TRACK_METADATA_PREFIX = "hm://metadata/3/track/";


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
  appendVarintField(buildInfo, 10u, CLIENT_PRODUCT_CLASS);  // PRODUCT_CLIENT
  appendVarintField(buildInfo, 30u, CLIENT_PLATFORM_CLASS); // PLATFORM_LINUX_X86
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
  appendVarintField(systemInfo, 10u, AUTH_CPU_CLASS);        // CPU_UNKNOWN
  appendVarintField(systemInfo, 60u, AUTH_OS_CLASS);         // OS_UNKNOWN
  appendStringField(systemInfo, 90u, String(AUTH_SYSTEM_NAME));
  appendStringField(systemInfo, 100u, String(deviceId ? deviceId : ""));

  std::vector<uint8_t> out;
  appendMessageField(out, 10u, login);
  appendMessageField(out, 50u, systemInfo);
  appendStringField(out, 70u, String(AUTH_CLIENT_VERSION));
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
                       uint8_t& command, std::vector<uint8_t>& payload, bool& macOk,
                       size_t* declaredPayload = nullptr, const char** failStage = nullptr) {
  if (declaredPayload) *declaredPayload = 0u;
  if (failStage) *failStage = "none";
  std::vector<uint8_t> header(3u, 0u);
  if (!readExact(client, header.data(), header.size(), timeoutMs)) {
    if (failStage) *failStage = "header";
    return false;
  }
  cipher.decrypt(header);
  command = header[0];
  const size_t payloadSize = (static_cast<size_t>(header[1]) << 8u) | header[2];
  if (declaredPayload) *declaredPayload = payloadSize;
  if (payloadSize > maxPayload) {
    if (failStage) *failStage = "oversize";
    return false;
  }

  payload.assign(payloadSize, 0u);
  if (payloadSize != 0u) {
    if (!readExact(client, payload.data(), payload.size(), timeoutMs)) {
      if (failStage) *failStage = "payload";
      return false;
    }
    cipher.decrypt(payload);
  }

  uint8_t receivedMac[SHANNON_MAC_BYTES];
  if (!readExact(client, receivedMac, sizeof(receivedMac), timeoutMs)) {
    if (failStage) *failStage = "mac-read";
    return false;
  }
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

bool extractProtoVarint(const uint8_t* data, size_t size, uint32_t wantedField, uint64_t& out) {
  size_t offset = 0u;
  while (offset < size) {
    uint64_t key = 0u;
    if (!readVarint(data, size, offset, key)) return false;
    const uint32_t field = static_cast<uint32_t>(key >> 3u);
    const uint8_t wire = static_cast<uint8_t>(key & 7u);
    if (wire == 0u) {
      uint64_t value = 0u;
      if (!readVarint(data, size, offset, value)) return false;
      if (field == wantedField) { out = value; return true; }
    } else if (wire == 1u) {
      if (size - offset < 8u) return false;
      offset += 8u;
    } else if (wire == 2u) {
      uint64_t len = 0u;
      if (!readVarint(data, size, offset, len) || len > size - offset) return false;
      offset += static_cast<size_t>(len);
    } else if (wire == 5u) {
      if (size - offset < 4u) return false;
      offset += 4u;
    } else {
      return false;
    }
  }
  return false;
}

int32_t decodeZigZag32(uint64_t value) {
  return static_cast<int32_t>((value >> 1u) ^ static_cast<uint64_t>(-static_cast<int64_t>(value & 1u)));
}

String bytesToHex(const uint8_t* data, size_t size) {
  static constexpr char kHexDigits[] = "0123456789abcdef";
  String out;
  out.reserve(static_cast<unsigned int>(size * 2u));
  for (size_t i = 0u; i < size; ++i) {
    out += kHexDigits[(data[i] >> 4u) & 0x0fu];
    out += kHexDigits[data[i] & 0x0fu];
  }
  return out;
}

String spotifyTrackUriFromGid(const uint8_t* gid, size_t size) {
  if (!gid || size != TRACK_GID_BYTES) return String();
  static constexpr char kBase62[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  uint8_t value[TRACK_GID_BYTES];
  memcpy(value, gid, sizeof(value));
  char encoded[23];
  encoded[22] = '\0';
  for (int out = 21; out >= 0; --out) {
    uint32_t remainder = 0u;
    for (size_t i = 0u; i < sizeof(value); ++i) {
      const uint32_t current = (remainder << 8u) | value[i];
      value[i] = static_cast<uint8_t>(current / 62u);
      remainder = current % 62u;
    }
    encoded[out] = kBase62[remainder];
  }
  return String(F("spotify:track:")) + encoded;
}


size_t findByteLiteral(const uint8_t* data, size_t size, const char* literal, size_t start = 0u) {
  if (!data || !literal || start >= size) return size;
  const size_t n = strlen(literal);
  if (n == 0u || n > size) return size;
  for (size_t i = start; i + n <= size; ++i) {
    if (memcmp(data + i, literal, n) == 0) return i;
  }
  return size;
}

bool jsonWhitespace(uint8_t c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool findJsonKeyValueStart(const uint8_t* data, size_t size, const char* key,
                           size_t& valueStart, size_t start = 0u,
                           size_t end = SIZE_MAX, size_t* keyPosOut = nullptr) {
  valueStart = size;
  if (!data || !key || start >= size) return false;
  if (end > size) end = size;
  String marker = String('"') + key + '"';
  size_t search = start;
  while (search < end) {
    const size_t pos = findByteLiteral(data, end, marker.c_str(), search);
    if (pos >= end) return false;
    size_t p = pos + marker.length();
    while (p < end && jsonWhitespace(data[p])) ++p;
    if (p < end && data[p] == ':') {
      ++p;
      while (p < end && jsonWhitespace(data[p])) ++p;
      if (p < end) {
        valueStart = p;
        if (keyPosOut) *keyPosOut = pos;
        return true;
      }
    }
    search = pos + 1u;
  }
  return false;
}

bool parseJsonStringAt(const uint8_t* data, size_t end, size_t valueStart, String& out) {
  out = String();
  if (!data || valueStart >= end || data[valueStart] != '"') return false;
  size_t p = valueStart + 1u;
  out.reserve(48u);
  bool escaped = false;
  while (p < end) {
    const char c = static_cast<char>(data[p++]);
    if (escaped) {
      // Spotify identifiers and URIs are ASCII. Preserve simple escapes; the
      // selector never needs to materialize arbitrary user-facing JSON text.
      out += c;
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else if (c == '"') {
      return true;
    } else {
      if (out.length() >= 192u) return false;
      out += c;
    }
  }
  out = String();
  return false;
}

bool jsonCompositeEnd(const uint8_t* data, size_t size, size_t start, size_t& endExclusive) {
  endExclusive = size;
  if (!data || start >= size || (data[start] != '{' && data[start] != '[')) return false;
  uint32_t objectDepth = 0u;
  uint32_t arrayDepth = 0u;
  bool inString = false;
  bool escaped = false;
  for (size_t i = start; i < size; ++i) {
    const uint8_t c = data[i];
    if (inString) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') inString = false;
      continue;
    }
    if (c == '"') { inString = true; continue; }
    if (c == '{') ++objectDepth;
    else if (c == '}') {
      if (objectDepth == 0u) return false;
      --objectDepth;
    } else if (c == '[') ++arrayDepth;
    else if (c == ']') {
      if (arrayDepth == 0u) return false;
      --arrayDepth;
    }
    if (objectDepth == 0u && arrayDepth == 0u) {
      endExclusive = i + 1u;
      return true;
    }
  }
  return false;
}

bool findEnclosingJsonObject(const uint8_t* data, size_t size, size_t target,
                             size_t& objectStart, size_t& objectEnd) {
  objectStart = objectEnd = size;
  if (!data || target >= size) return false;
  size_t stack[32];
  size_t depth = 0u;
  bool inString = false;
  bool escaped = false;
  for (size_t i = 0u; i <= target && i < size; ++i) {
    const uint8_t c = data[i];
    if (inString) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') inString = false;
      continue;
    }
    if (c == '"') { inString = true; continue; }
    if (c == '{') {
      if (depth >= (sizeof(stack) / sizeof(stack[0]))) return false;
      stack[depth++] = i;
    } else if (c == '}') {
      if (depth == 0u) return false;
      --depth;
    }
  }
  if (depth == 0u) return false;
  objectStart = stack[depth - 1u];
  return jsonCompositeEnd(data, size, objectStart, objectEnd) && objectEnd > target;
}

bool extractJsonQuotedValue(const uint8_t* data, size_t size, const char* key,
                            String& out, size_t start = 0u, size_t end = SIZE_MAX) {
  out = String();
  if (end > size) end = size;
  size_t valueStart = size;
  if (!findJsonKeyValueStart(data, size, key, valueStart, start, end)) return false;
  return parseJsonStringAt(data, end, valueStart, out);
}

bool extractJsonUintValue(const uint8_t* data, size_t size, const char* key,
                          uint32_t& out, size_t start = 0u, size_t end = SIZE_MAX) {
  if (end > size) end = size;
  size_t p = size;
  if (!findJsonKeyValueStart(data, size, key, p, start, end)) return false;
  bool quoted = p < end && data[p] == '"';
  if (quoted) ++p;
  uint64_t value = 0u;
  bool any = false;
  while (p < end && data[p] >= '0' && data[p] <= '9') {
    any = true;
    value = value * 10u + static_cast<uint64_t>(data[p++] - '0');
    if (value > 0xffffffffu) return false;
  }
  if (quoted && (p >= end || data[p] != '"')) return false;
  if (!any) return false;
  out = static_cast<uint32_t>(value);
  return true;
}

bool resolveContextPlayerUidToUri(const uint8_t* data, size_t size,
                                  const String& uid, String& uri) {
  uri = String();
  if (!data || uid.length() == 0u) return false;

  // The play command commonly identifies skip_to only by track_uid while the
  // matching URI lives in context.pages[].tracks[]. Walk actual JSON `uid`
  // members, find the enclosing track object, then resolve its sibling `uri`.
  // This is whitespace/order tolerant and avoids guessing from neighbouring
  // tracks or exposing the context body.
  size_t search = 0u;
  while (search < size) {
    size_t valueStart = size;
    size_t keyPos = size;
    if (!findJsonKeyValueStart(data, size, "uid", valueStart, search, size, &keyPos)) return false;
    String candidate;
    if (parseJsonStringAt(data, size, valueStart, candidate) && candidate == uid) {
      size_t objectStart = size;
      size_t objectEnd = size;
      if (findEnclosingJsonObject(data, size, keyPos, objectStart, objectEnd)) {
        String candidateUri;
        if (extractJsonQuotedValue(data, size, "uri", candidateUri, objectStart, objectEnd) &&
            candidateUri.startsWith(F("spotify:track:"))) {
          uri = candidateUri;
          return true;
        }
      }
    }
    search = valueStart < size ? valueStart + 1u : keyPos + 1u;
  }
  return false;
}


struct ContextPlayerProtoSummary {
  bool valid = false;
  uint32_t fields = 0u;
  uint32_t lengthFields = 0u;
  String topMap;
};

uint32_t fnv1a32Update(uint32_t h, const uint8_t* data, size_t size) {
  if (!data) return h;
  for (size_t i = 0u; i < size; ++i) {
    h ^= data[i];
    h *= 16777619u;
  }
  return h;
}

uint32_t fnv1a32(const uint8_t* data, size_t size) {
  return fnv1a32Update(2166136261u, data, size);
}

bool bytesContain(const uint8_t* haystack, size_t haystackSize,
                  const uint8_t* needle, size_t needleSize) {
  if (!haystack || !needle || needleSize == 0u || needleSize > haystackSize) return false;
  const size_t last = haystackSize - needleSize;
  for (size_t i = 0u; i <= last; ++i) {
    if (memcmp(haystack + i, needle, needleSize) == 0) return true;
  }
  return false;
}

String contextPlayerPrefixHex(const uint8_t* data, size_t size) {
  static const char HEX_DIGITS[] = "0123456789abcdef";
  String out;
  const size_t n = std::min<size_t>(size, 16u);
  out.reserve(n * 2u);
  for (size_t i = 0u; i < n; ++i) {
    out += HEX_DIGITS[(data[i] >> 4u) & 0x0fu];
    out += HEX_DIGITS[data[i] & 0x0fu];
  }
  return out;
}

String contextPlayerMagic(const uint8_t* data, size_t size) {
  if (!data || size == 0u) return F("none");
  if (size >= 2u && data[0] == 0x1fu && data[1] == 0x8bu) return F("gzip");
  if (size >= 4u && data[0] == 0x28u && data[1] == 0xb5u && data[2] == 0x2fu && data[3] == 0xfdu) return F("zstd");
  if (size >= 2u && data[0] == 0x78u && (((static_cast<uint16_t>(data[0]) << 8u) | data[1]) % 31u) == 0u) return F("zlib");
  size_t first = 0u;
  while (first < size && (data[first] == ' ' || data[first] == '\t' || data[first] == '\r' || data[first] == '\n')) ++first;
  if (first < size && (data[first] == '{' || data[first] == '[')) return F("json");
  return F("none");
}

uint32_t contextPlayerPrintablePct(const uint8_t* data, size_t size) {
  if (!data || size == 0u) return 0u;
  size_t printable = 0u;
  for (size_t i = 0u; i < size; ++i) {
    const uint8_t c = data[i];
    if ((c >= 0x20u && c <= 0x7eu) || c == '\r' || c == '\n' || c == '\t') ++printable;
  }
  return static_cast<uint32_t>((printable * 100u) / size);
}

struct ContextPlayerInflateResult {
  uint8_t* data = nullptr;
  size_t size = 0u;
  bool attempted = false;
  bool ok = false;
  bool crcOk = false;
  const char* status = "idle";
};

uint32_t readLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8u) |
         (static_cast<uint32_t>(p[2]) << 16u) |
         (static_cast<uint32_t>(p[3]) << 24u);
}

void freeContextPlayerInflate(ContextPlayerInflateResult& result) {
  if (!result.data) return;
#if SPOTIFY_HAVE_HEAP_CAPS
  heap_caps_free(result.data);
#else
  free(result.data);
#endif
  result.data = nullptr;
  result.size = 0u;
}

ContextPlayerInflateResult inflateContextPlayerGzip(const uint8_t* data, size_t size) {
  ContextPlayerInflateResult result;
  if (!data || size < 18u || data[0] != 0x1fu || data[1] != 0x8bu) return result;
  result.attempted = true;
#if !SPOTIFY_HAVE_ROM_MINIZ
  result.status = "miniz-unavailable";
  return result;
#else
  if (data[2] != 8u || (data[3] & 0xe0u) != 0u) {
    result.status = "bad-header";
    return result;
  }

  const uint8_t flags = data[3];
  size_t offset = 10u;
  const size_t trailerOffset = size - 8u;
  if ((flags & 0x04u) != 0u) {
    if (offset + 2u > trailerOffset) { result.status = "bad-extra"; return result; }
    const size_t extra = static_cast<size_t>(data[offset]) | (static_cast<size_t>(data[offset + 1u]) << 8u);
    offset += 2u;
    if (extra > trailerOffset - offset) { result.status = "bad-extra"; return result; }
    offset += extra;
  }
  auto skipZeroTerminated = [&](const char* status) -> bool {
    while (offset < trailerOffset && data[offset] != 0u) ++offset;
    if (offset >= trailerOffset) { result.status = status; return false; }
    ++offset;
    return true;
  };
  if ((flags & 0x08u) != 0u && !skipZeroTerminated("bad-name")) return result;
  if ((flags & 0x10u) != 0u && !skipZeroTerminated("bad-comment")) return result;
  if ((flags & 0x02u) != 0u) {
    if (offset + 2u > trailerOffset) { result.status = "bad-hcrc"; return result; }
    offset += 2u;
  }
  if (offset >= trailerOffset) { result.status = "empty-deflate"; return result; }

  const uint32_t expectedCrc = readLe32(data + trailerOffset);
  const uint32_t expectedSize = readLe32(data + trailerOffset + 4u);
  if (expectedSize == 0u || expectedSize > MAX_CONTEXT_PLAYER_INFLATED_BYTES) {
    result.status = "size-bound";
    return result;
  }

#if SPOTIFY_HAVE_HEAP_CAPS
  result.data = static_cast<uint8_t*>(heap_caps_malloc(expectedSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!result.data && expectedSize <= 32768u) {
    result.data = static_cast<uint8_t*>(heap_caps_malloc(expectedSize, MALLOC_CAP_8BIT));
  }
#else
  if (expectedSize <= 32768u) result.data = static_cast<uint8_t*>(malloc(expectedSize));
#endif
  if (!result.data) { result.status = "alloc-failed"; return result; }

  // Do not use tinfl_decompress_mem_to_mem() here. Espressif's miniz helper
  // creates a tinfl_decompressor as a local variable (~11 KiB on this miniz
  // layout), which can overflow the already-active Spotify AP task stack.
  // Keep the decompressor state on the heap and call the ROM low-level API
  // directly; the large decompressed body remains in PSRAM when available.
  tinfl_decompressor* decomp = nullptr;
#if SPOTIFY_HAVE_HEAP_CAPS
  decomp = static_cast<tinfl_decompressor*>(
      heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!decomp) {
    decomp = static_cast<tinfl_decompressor*>(
        heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_8BIT));
  }
#else
  decomp = static_cast<tinfl_decompressor*>(malloc(sizeof(tinfl_decompressor)));
#endif
  if (!decomp) {
    result.status = "state-alloc-failed";
    freeContextPlayerInflate(result);
    return result;
  }

  tinfl_init(decomp);
  size_t inputBytes = trailerOffset - offset;
  size_t outputBytes = expectedSize;
  const tinfl_status inflateStatus = tinfl_decompress(
      decomp, data + offset, &inputBytes, result.data, result.data, &outputBytes,
      TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
#if SPOTIFY_HAVE_HEAP_CAPS
  heap_caps_free(decomp);
#else
  free(decomp);
#endif

  if (inflateStatus != TINFL_STATUS_DONE || outputBytes != expectedSize) {
    result.status = "inflate-failed";
    freeContextPlayerInflate(result);
    return result;
  }

  result.size = outputBytes;
  const uint32_t actualCrc = static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, result.data, result.size));
  result.crcOk = actualCrc == expectedCrc;
  if (!result.crcOk) {
    result.status = "crc-mismatch";
    freeContextPlayerInflate(result);
    return result;
  }
  result.ok = true;
  result.status = "ok";
  return result;
#endif
}

ContextPlayerProtoSummary summarizeContextPlayerProto(const uint8_t* data, size_t size) {
  ContextPlayerProtoSummary out;
  if (!data || size == 0u) return out;
  size_t offset = 0u;
  String map;
  map.reserve(72u);
  uint32_t mapped = 0u;
  while (offset < size && out.fields < 256u) {
    uint64_t key = 0u;
    if (!readVarint(data, size, offset, key)) return out;
    const uint32_t field = static_cast<uint32_t>(key >> 3u);
    const uint8_t wire = static_cast<uint8_t>(key & 7u);
    if (field == 0u || (wire != 0u && wire != 1u && wire != 2u && wire != 5u)) return out;
    ++out.fields;
    if (mapped < 10u) {
      if (map.length() != 0u) map += ',';
      map += String(field);
      map += ':';
      map += String(wire);
      ++mapped;
    }
    if (wire == 0u) {
      uint64_t ignored = 0u;
      if (!readVarint(data, size, offset, ignored)) return out;
    } else if (wire == 1u) {
      if (size - offset < 8u) return out;
      offset += 8u;
    } else if (wire == 2u) {
      uint64_t len64 = 0u;
      if (!readVarint(data, size, offset, len64) || len64 > size - offset) return out;
      ++out.lengthFields;
      offset += static_cast<size_t>(len64);
    } else if (wire == 5u) {
      if (size - offset < 4u) return out;
      offset += 4u;
    }
  }
  if (offset != size || out.fields == 0u) return out;
  out.valid = true;
  out.topMap = map;
  return out;
}

bool parseModernContextPlayerState(const uint8_t* data, size_t size, String& targetUid,
                                   String& targetUri, uint32_t& targetIndex,
                                   bool& hasTargetIndex) {
  targetUid = String();
  targetUri = String();
  targetIndex = 0u;
  hasTargetIndex = false;
  if (!data || size == 0u) return false;

  bool sawKnownField = false;

  // spotify.player.esperanto.proto.ContextPlayerState.index = field 6.
  // ContextIndex.track = field 2. Prefer the track URI below for matching because
  // this index can be page-relative on large contexts.
  std::vector<uint8_t> indexMessage;
  if (extractLengthDelimited(data, size, 6u, indexMessage)) {
    uint64_t track = 0u;
    if (extractProtoVarint(indexMessage.data(), indexMessage.size(), 2u, track) &&
        track <= 0xffffffffu) {
      targetIndex = static_cast<uint32_t>(track);
      hasTargetIndex = true;
      sawKnownField = true;
    }
  }

  // ContextPlayerState.track = ProvidedTrack field 7; ProvidedTrack.context_track
  // = field 1; ContextTrack.uri/uid = fields 1/2. This is the current binary
  // encoding seen from Android, not the historical JSON command payload.
  std::vector<uint8_t> providedTrack;
  std::vector<uint8_t> contextTrack;
  if (extractLengthDelimited(data, size, 7u, providedTrack) &&
      extractLengthDelimited(providedTrack.data(), providedTrack.size(), 1u, contextTrack)) {
    String uri;
    if (extractProtoString(contextTrack.data(), contextTrack.size(), 1u, uri) &&
        uri.startsWith(F("spotify:track:"))) {
      targetUri = uri;
      sawKnownField = true;
    }
    String uid;
    if (extractProtoString(contextTrack.data(), contextTrack.size(), 2u, uid)) {
      targetUid = uid;
      sawKnownField = true;
    }
  }

  return sawKnownField;
}

void parseContextPlayerState(const uint8_t* data, size_t size, String& encoding,
                             String& endpoint, String& targetUid, String& targetUri,
                             uint32_t& targetIndex, bool& hasTargetIndex,
                             bool& resolvedUidToUri) {
  encoding = String();
  endpoint = String();
  targetUid = String();
  targetUri = String();
  targetIndex = 0u;
  hasTargetIndex = false;
  resolvedUidToUri = false;
  if (!data || size == 0u) return;

  size_t first = 0u;
  while (first < size && (data[first] == ' ' || data[first] == '\t' ||
                          data[first] == '\r' || data[first] == '\n')) ++first;

  if (first < size && data[first] == '{') {
    encoding = F("json");
    extractJsonQuotedValue(data, size, "endpoint", endpoint);

    size_t skipValue = size;
    size_t skipEnd = size;
    if (findJsonKeyValueStart(data, size, "skip_to", skipValue)) {
      if (skipValue < size && (data[skipValue] == '{' || data[skipValue] == '[')) {
        if (!jsonCompositeEnd(data, size, skipValue, skipEnd)) skipEnd = size;
      } else {
        skipEnd = std::min(size, skipValue + 2048u);
      }
      if (!extractJsonQuotedValue(data, size, "track_uri", targetUri, skipValue, skipEnd)) {
        extractJsonQuotedValue(data, size, "uri", targetUri, skipValue, skipEnd);
        if (!targetUri.startsWith(F("spotify:track:"))) targetUri = String();
      }
      extractJsonQuotedValue(data, size, "track_uid", targetUid, skipValue, skipEnd);
      hasTargetIndex = extractJsonUintValue(data, size, "track_index", targetIndex, skipValue, skipEnd);
    }
    if (targetUri.length() == 0u && targetUid.length() != 0u) {
      resolvedUidToUri = resolveContextPlayerUidToUri(data, size, targetUid, targetUri);
    }
    return;
  }

  if (parseModernContextPlayerState(data, size, targetUid, targetUri,
                                    targetIndex, hasTargetIndex)) {
    encoding = F("proto");
    return;
  }

  encoding = F("binary");
}

std::vector<uint8_t> buildAudioKeyRequest(const uint8_t fileId[AUDIO_FILE_ID_BYTES],
                                          const uint8_t trackGid[TRACK_GID_BYTES],
                                          uint32_t sequence) {
  std::vector<uint8_t> out;
  if (!fileId || !trackGid) return out;
  out.reserve(AUDIO_FILE_ID_BYTES + TRACK_GID_BYTES + 6u);
  out.insert(out.end(), fileId, fileId + AUDIO_FILE_ID_BYTES);
  out.insert(out.end(), trackGid, trackGid + TRACK_GID_BYTES);
  out.push_back(static_cast<uint8_t>((sequence >> 24u) & 0xffu));
  out.push_back(static_cast<uint8_t>((sequence >> 16u) & 0xffu));
  out.push_back(static_cast<uint8_t>((sequence >> 8u) & 0xffu));
  out.push_back(static_cast<uint8_t>(sequence & 0xffu));
  out.push_back(0x00u);
  out.push_back(0x00u);
  return out;
}

std::vector<uint8_t> buildApStreamChunkRequest(uint16_t channelId,
                                               const uint8_t fileId[AUDIO_FILE_ID_BYTES],
                                               uint32_t offsetWords,
                                               uint32_t sizeWords) {
  std::vector<uint8_t> out;
  if (!fileId || sizeWords == 0u || offsetWords > 0xffffffffu - sizeWords) return out;
  out.reserve(46u);
  appendBe16(out, channelId);
  out.push_back(0x00u);
  out.push_back(0x01u);
  appendBe16(out, 0x0000u);
  appendBe32(out, 0x00000000u);
  appendBe32(out, 0x00009C40u);
  appendBe32(out, 0x00020000u);
  out.insert(out.end(), fileId, fileId + AUDIO_FILE_ID_BYTES);
  appendBe32(out, offsetWords);
  appendBe32(out, offsetWords + sizeWords);
  return out;
}

bool readBe32Prefix(const std::vector<uint8_t>& data, uint32_t& value) {
  if (data.size() < 4u) return false;
  value = (static_cast<uint32_t>(data[0]) << 24u) |
          (static_cast<uint32_t>(data[1]) << 16u) |
          (static_cast<uint32_t>(data[2]) << 8u) |
          static_cast<uint32_t>(data[3]);
  return true;
}

struct LegacyTrackMetadataInfo {
  struct AudioFileCandidate {
    int32_t format = -1;
    std::vector<uint8_t> fileId;
  };

  String title;
  String artists;
  String album;
  uint32_t durationMs = 0u;
  uint32_t coverCount = 0u;
  std::vector<uint8_t> coverId;
  uint32_t audioFileCount = 0u;
  int32_t preferredFormat = -1;
  std::vector<uint8_t> preferredFileId;
  std::vector<AudioFileCandidate> audioFiles;
};

bool parseLegacyAlbum(const uint8_t* data, size_t size, LegacyTrackMetadataInfo& info) {
  size_t offset = 0u;
  int bestCoverSize = -1;
  while (offset < size) {
    uint64_t key = 0u;
    if (!readVarint(data, size, offset, key)) return false;
    const uint32_t field = static_cast<uint32_t>(key >> 3u);
    const uint8_t wire = static_cast<uint8_t>(key & 7u);
    if (wire == 0u) {
      uint64_t ignored = 0u;
      if (!readVarint(data, size, offset, ignored)) return false;
    } else if (wire == 1u) {
      if (size - offset < 8u) return false;
      offset += 8u;
    } else if (wire == 2u) {
      uint64_t len64 = 0u;
      if (!readVarint(data, size, offset, len64) || len64 > size - offset) return false;
      const size_t len = static_cast<size_t>(len64);
      if (field == 2u) {
        info.album = String();
        info.album.reserve(static_cast<unsigned int>(len));
        for (size_t i = 0u; i < len; ++i) info.album += static_cast<char>(data[offset + i]);
      } else if (field == 9u) {
        ++info.coverCount;
        std::vector<uint8_t> fileId;
        uint64_t imageSize = 0u;
        extractLengthDelimited(data + offset, len, 1u, fileId);
        extractProtoVarint(data + offset, len, 2u, imageSize);
        if (!fileId.empty() && static_cast<int>(imageSize) >= bestCoverSize) {
          bestCoverSize = static_cast<int>(imageSize);
          info.coverId = fileId;
        }
      }
      offset += len;
    } else if (wire == 5u) {
      if (size - offset < 4u) return false;
      offset += 4u;
    } else {
      return false;
    }
  }
  return true;
}

bool parseLegacyTrackMetadata(const std::vector<uint8_t>& data, LegacyTrackMetadataInfo& info) {
  size_t offset = 0u;
  bool sawUsefulField = false;
  while (offset < data.size()) {
    uint64_t key = 0u;
    if (!readVarint(data.data(), data.size(), offset, key)) return false;
    const uint32_t field = static_cast<uint32_t>(key >> 3u);
    const uint8_t wire = static_cast<uint8_t>(key & 7u);
    if (wire == 0u) {
      uint64_t value = 0u;
      if (!readVarint(data.data(), data.size(), offset, value)) return false;
      if (field == 7u) {
        const int32_t decoded = decodeZigZag32(value);
        info.durationMs = decoded > 0 ? static_cast<uint32_t>(decoded) : 0u;
        sawUsefulField = true;
      }
    } else if (wire == 1u) {
      if (data.size() - offset < 8u) return false;
      offset += 8u;
    } else if (wire == 2u) {
      uint64_t len64 = 0u;
      if (!readVarint(data.data(), data.size(), offset, len64) || len64 > data.size() - offset) return false;
      const size_t len = static_cast<size_t>(len64);
      const uint8_t* item = data.data() + offset;
      if (field == 2u) {
        info.title = String();
        info.title.reserve(static_cast<unsigned int>(len));
        for (size_t i = 0u; i < len; ++i) info.title += static_cast<char>(item[i]);
        sawUsefulField = true;
      } else if (field == 3u) {
        if (!parseLegacyAlbum(item, len, info)) return false;
      } else if (field == 4u) {
        String artist;
        if (extractProtoString(item, len, 2u, artist) && artist.length() != 0u) {
          if (info.artists.length() != 0u) info.artists += F(", ");
          if (info.artists.length() < 112u) info.artists += artist;
          sawUsefulField = true;
        }
      } else if (field == 12u) {
        ++info.audioFileCount;
        std::vector<uint8_t> fileId;
        uint64_t format = 0u;
        extractLengthDelimited(item, len, 1u, fileId);
        const bool hasFormat = extractProtoVarint(item, len, 2u, format);
        if (fileId.size() == AUDIO_FILE_ID_BYTES) {
          LegacyTrackMetadataInfo::AudioFileCandidate candidate;
          candidate.format = hasFormat ? static_cast<int32_t>(format) : -1;
          candidate.fileId = fileId;
          info.audioFiles.push_back(candidate);
          const bool prefer = info.preferredFileId.empty() ||
                              (hasFormat && format == 1u && info.preferredFormat != 1);
          if (prefer) {
            info.preferredFileId = fileId;
            info.preferredFormat = hasFormat ? static_cast<int32_t>(format) : -1;
          }
        }
      }
      offset += len;
    } else if (wire == 5u) {
      if (data.size() - offset < 4u) return false;
      offset += 4u;
    } else {
      return false;
    }
  }
  return sawUsefulField && info.title.length() != 0u;
}

std::vector<uint8_t> buildSpircCapability(uint32_t type, int64_t intValue,
                                          const std::vector<String>& stringValues) {
  std::vector<uint8_t> out;
  appendVarintField(out, 1u, type);
  if (intValue >= 0) appendVarintField(out, 2u, static_cast<uint64_t>(intValue));
  for (const String& value : stringValues) appendStringField(out, 3u, value);
  return out;
}

std::vector<uint8_t> buildSpircDeviceState(const char* deviceName, uint16_t volume,
                                          bool active = false, uint64_t becameActiveAt = 0u) {
  std::vector<uint8_t> out;
  appendStringField(out, 1u, String(F("wled-spotify-dev2g")));
  appendVarintField(out, 10u, active ? 1u : 0u);
  appendVarintField(out, 11u, 1u); // can_play=true: this gate can accept SPIRC control
  appendVarintField(out, 12u, volume);
  appendStringField(out, 13u, String(deviceName && *deviceName ? deviceName : "WLED Matrix"));
  if (active && becameActiveAt != 0u) appendVarintField(out, 15u, becameActiveAt);

  auto addIntCapability = [&out](uint32_t type, int64_t value) {
    appendMessageField(out, 17u, buildSpircCapability(type, value, {}));
  };
  addIntCapability(2u, 1);   // kCanBePlayer
  addIntCapability(4u, 4);   // kDeviceType: speaker/computer-compatible value used by cspot
  addIntCapability(5u, 1);   // kGaiaEqConnectId
  addIntCapability(6u, 0);   // kSupportsLogout
  // kSupportsPlaylistV2 (type 13) is intentionally NOT advertised. This build
  // implements classic SPIRC State.track/commands but not the full playlist-v2 /
  // connect-state command contract. Advertising it made current Android choose a
  // control path this receiver could not honor for direct list selection.
  addIntCapability(10u, 1);  // kCommandAcks: Notify echoes recipient command ident/seq
  addIntCapability(7u, 1);   // kIsObservable
  addIntCapability(8u, 64);  // kVolumeSteps
  appendMessageField(out, 17u, buildSpircCapability(1u, -1, {
      String(F("album")), String(F("playlist")), String(F("search")),
      String(F("inbox")), String(F("toplist")), String(F("starred")),
      String(F("publishedstarred")), String(F("track"))}));
  appendMessageField(out, 17u, buildSpircCapability(9u, -1, {
      String(F("audio/track")), String(F("audio/episode")),
      String(F("audio/episode+track"))}));
  return out;
}

std::vector<uint8_t> buildSpircState(uint64_t syncedTimestampMs) {
  std::vector<uint8_t> out;
  appendVarintField(out, 4u, 0u);                  // position_ms
  appendVarintField(out, 5u, 0u);                  // kPlayStatusStop
  appendVarintField(out, 7u, 0u);                   // position_measured_at for initial stopped state
  appendVarintField(out, 13u, 0u);                 // shuffle=false
  appendVarintField(out, 14u, 0u);                 // repeat=false
  return out;
}

std::vector<uint8_t> buildSpircFrame(uint32_t type, uint32_t sequence,
                                     const char* deviceId, const char* deviceName,
                                     uint16_t volume, uint64_t syncedTimestampMs) {
  std::vector<uint8_t> out;
  appendVarintField(out, 1u, 1u);
  appendStringField(out, 2u, String(deviceId ? deviceId : ""));
  appendStringField(out, 3u, String(SPIRC_PROTOCOL_VERSION));
  appendVarintField(out, 4u, sequence);
  appendVarintField(out, 5u, type);
  appendMessageField(out, 7u, buildSpircDeviceState(deviceName, volume));
  appendMessageField(out, 12u, buildSpircState(syncedTimestampMs));
  appendVarintField(out, 17u, syncedTimestampMs);
  return out;
}

struct SpircFrameInfo {
  uint32_t type = 0u;
  uint32_t seqNr = 0u;
  bool hasSeqNr = false;
  std::vector<String> recipients;
  String lastCommandIdent;
  uint32_t lastCommandMsgid = 0u;
  bool hasLastCommandAck = false;
  String ident;
  String name;
  bool active = false;
  bool hasActive = false;
  uint32_t volume = 0u;
  bool hasVolume = false;
  uint32_t position = 0u;
  bool hasPosition = false;
  String contextUri;
  uint32_t positionMs = 0u;
  bool hasPositionMs = false;
  uint32_t playStatus = 0u;
  bool hasPlayStatus = false;
  uint32_t stateIndex = 0u;
  bool hasStateIndex = false;
  uint32_t playingTrackIndex = 0u;
  bool hasPlayingTrackIndex = false;
  uint32_t trackCount = 0u;
  uint32_t selectedTrackIndex = 0u;
  std::vector<uint8_t> selectedTrackGid;
  String selectedTrackUri;
  std::vector<std::vector<uint8_t>> trackRefs;
  size_t trackRefBytes = 0u;
  uint32_t trackRefsTruncated = 0u;
  bool shuffle = false;
  bool hasShuffle = false;
  bool repeat = false;
  bool hasRepeat = false;
  bool hasContextPlayerState = false;
  size_t contextPlayerStateBytes = 0u;
  String contextPlayerEncoding;
  String contextPlayerEndpoint;
  String contextPlayerTargetUid;
  String contextPlayerTargetUri;
  uint32_t contextPlayerTargetIndex = 0u;
  bool hasContextPlayerTargetIndex = false;
  bool contextPlayerUidResolved = false;
  std::vector<uint8_t> contextPlayerState;
  uint32_t contextPlayerHash = 0u;
  String contextPlayerPrefix;
  String contextPlayerMagic;
  uint32_t contextPlayerPrintablePct = 0u;
  bool contextPlayerProtoValid = false;
  uint32_t contextPlayerProtoFields = 0u;
  uint32_t contextPlayerProtoLengthFields = 0u;
  String contextPlayerProtoMap;
};

bool parseSpircFrame(const std::vector<uint8_t>& data, SpircFrameInfo& info) {
  uint64_t type = 0u;
  if (!extractProtoVarint(data.data(), data.size(), 5u, type)) return false;
  info.type = static_cast<uint32_t>(type);
  extractProtoString(data.data(), data.size(), 2u, info.ident);
  uint64_t seqNr = 0u;
  if (extractProtoVarint(data.data(), data.size(), 4u, seqNr)) {
    info.seqNr = static_cast<uint32_t>(seqNr);
    info.hasSeqNr = true;
  }

  // Frame.recipient is repeated string field 18. Keep a small bounded set
  // so commands for other Connect devices are ignored and targeted commands
  // can be acknowledged through State.last_command_* (fields 20/21).
  size_t frameOffset = 0u;
  while (frameOffset < data.size()) {
    uint64_t frameKey = 0u;
    if (!readVarint(data.data(), data.size(), frameOffset, frameKey)) break;
    const uint32_t frameField = static_cast<uint32_t>(frameKey >> 3u);
    const uint8_t frameWire = static_cast<uint8_t>(frameKey & 7u);
    if (frameWire == 0u) {
      uint64_t ignored = 0u;
      if (!readVarint(data.data(), data.size(), frameOffset, ignored)) break;
    } else if (frameWire == 1u) {
      if (data.size() - frameOffset < 8u) break;
      frameOffset += 8u;
    } else if (frameWire == 2u) {
      uint64_t len64 = 0u;
      if (!readVarint(data.data(), data.size(), frameOffset, len64) || len64 > data.size() - frameOffset) break;
      const size_t len = static_cast<size_t>(len64);
      if (frameField == 18u && info.recipients.size() < 4u) {
        String recipient;
        recipient.reserve(static_cast<unsigned int>(len));
        for (size_t i = 0u; i < len; ++i) recipient += static_cast<char>(data[frameOffset + i]);
        info.recipients.push_back(recipient);
      } else if (frameField == 19u) {
        info.hasContextPlayerState = true;
        info.contextPlayerStateBytes = len;
        const uint8_t* cps = data.data() + frameOffset;
        // Keep only this bounded frame-local copy. It is discarded after the
        // SPIRC event is handled and is never surfaced in diagnostics. The copy
        // lets the resolver compare opaque current-Android payloads against the
        // already-retained classic TrackRef queue without guessing a schema.
        if (len <= MAX_SPIRC_STATE_TRACK_REF_BYTES) {
          info.contextPlayerState.assign(cps, cps + len);
        }
        info.contextPlayerHash = fnv1a32(cps, len);
        info.contextPlayerPrefix = contextPlayerPrefixHex(cps, len);
        info.contextPlayerMagic = contextPlayerMagic(cps, len);
        info.contextPlayerPrintablePct = contextPlayerPrintablePct(cps, len);
        const ContextPlayerProtoSummary summary = summarizeContextPlayerProto(cps, len);
        info.contextPlayerProtoValid = summary.valid;
        info.contextPlayerProtoFields = summary.fields;
        info.contextPlayerProtoLengthFields = summary.lengthFields;
        info.contextPlayerProtoMap = summary.topMap;
        parseContextPlayerState(cps, len,
                                info.contextPlayerEncoding, info.contextPlayerEndpoint, info.contextPlayerTargetUid,
                                info.contextPlayerTargetUri, info.contextPlayerTargetIndex,
                                info.hasContextPlayerTargetIndex, info.contextPlayerUidResolved);
      }
      frameOffset += len;
    } else if (frameWire == 5u) {
      if (data.size() - frameOffset < 4u) break;
      frameOffset += 4u;
    } else {
      break;
    }
  }
  if (!info.recipients.empty() && info.ident.length() != 0u && info.hasSeqNr) {
    info.lastCommandIdent = info.ident;
    info.lastCommandMsgid = info.seqNr;
    info.hasLastCommandAck = true;
  }

  std::vector<uint8_t> deviceState;
  if (extractLengthDelimited(data.data(), data.size(), 7u, deviceState)) {
    uint64_t active = 0u;
    if (extractProtoVarint(deviceState.data(), deviceState.size(), 10u, active)) {
      info.active = active != 0u;
      info.hasActive = true;
    }
    uint64_t volume = 0u;
    if (extractProtoVarint(deviceState.data(), deviceState.size(), 12u, volume)) {
      info.volume = static_cast<uint32_t>(volume);
      info.hasVolume = true;
    }
    extractProtoString(deviceState.data(), deviceState.size(), 13u, info.name);
  }

  uint64_t position = 0u;
  if (extractProtoVarint(data.data(), data.size(), 13u, position)) {
    info.position = static_cast<uint32_t>(position);
    info.hasPosition = true;
  }

  std::vector<uint8_t> state;
  if (extractLengthDelimited(data.data(), data.size(), 12u, state)) {
    extractProtoString(state.data(), state.size(), 2u, info.contextUri);
    uint64_t value = 0u;
    if (extractProtoVarint(state.data(), state.size(), 4u, value)) {
      info.positionMs = static_cast<uint32_t>(value);
      info.hasPositionMs = true;
    }
    if (extractProtoVarint(state.data(), state.size(), 5u, value)) {
      info.playStatus = static_cast<uint32_t>(value);
      info.hasPlayStatus = true;
    }
    if (extractProtoVarint(state.data(), state.size(), 3u, value)) {
      info.stateIndex = static_cast<uint32_t>(value);
      info.hasStateIndex = true;
    }
    if (extractProtoVarint(state.data(), state.size(), 26u, value)) {
      info.playingTrackIndex = static_cast<uint32_t>(value);
      info.hasPlayingTrackIndex = true;
    }
    if (extractProtoVarint(state.data(), state.size(), 13u, value)) {
      info.shuffle = value != 0u;
      info.hasShuffle = true;
    }
    if (extractProtoVarint(state.data(), state.size(), 14u, value)) {
      info.repeat = value != 0u;
      info.hasRepeat = true;
    }

    const uint32_t wantedTrack = info.hasPlayingTrackIndex ? info.playingTrackIndex :
                                 (info.hasStateIndex ? info.stateIndex : 0u);
    std::vector<uint8_t> firstTrackGid;
    String firstTrackUri;
    size_t offset = 0u;
    while (offset < state.size()) {
      uint64_t key = 0u;
      if (!readVarint(state.data(), state.size(), offset, key)) break;
      const uint32_t field = static_cast<uint32_t>(key >> 3u);
      const uint8_t wire = static_cast<uint8_t>(key & 7u);
      if (wire == 0u) {
        uint64_t ignored = 0u;
        if (!readVarint(state.data(), state.size(), offset, ignored)) break;
      } else if (wire == 1u) {
        if (state.size() - offset < 8u) break;
        offset += 8u;
      } else if (wire == 2u) {
        uint64_t len64 = 0u;
        if (!readVarint(state.data(), state.size(), offset, len64) || len64 > state.size() - offset) break;
        const size_t len = static_cast<size_t>(len64);
        if (field == 27u) {
          std::vector<uint8_t> gid;
          String uri;
          extractLengthDelimited(state.data() + offset, len, 1u, gid);
          extractProtoString(state.data() + offset, len, 2u, uri);
          if (info.trackRefs.size() < MAX_SPIRC_STATE_TRACK_REFS &&
              info.trackRefBytes + len <= MAX_SPIRC_STATE_TRACK_REF_BYTES) {
            info.trackRefs.emplace_back(state.begin() + offset, state.begin() + offset + len);
            info.trackRefBytes += len;
          } else {
            ++info.trackRefsTruncated;
          }
          if (info.trackCount == 0u) { firstTrackGid = gid; firstTrackUri = uri; }
          if (info.trackCount == wantedTrack) {
            info.selectedTrackIndex = info.trackCount;
            info.selectedTrackGid = gid;
            info.selectedTrackUri = uri;
          }
          ++info.trackCount;
        }
        offset += len;
      } else if (wire == 5u) {
        if (state.size() - offset < 4u) break;
        offset += 4u;
      } else {
        break;
      }
    }
    if (info.selectedTrackGid.empty() && info.selectedTrackUri.length() == 0u && info.trackCount != 0u) {
      info.selectedTrackIndex = 0u;
      info.selectedTrackGid = firstTrackGid;
      info.selectedTrackUri = firstTrackUri;
    }
  }
  return true;
}

bool decodeSpircTrackRef(const std::vector<uint8_t>& trackRef,
                         std::vector<uint8_t>& gid, String& uri, String& context) {
  gid.clear();
  uri = String();
  context = String();
  extractLengthDelimited(trackRef.data(), trackRef.size(), 1u, gid);
  extractProtoString(trackRef.data(), trackRef.size(), 2u, uri);
  extractProtoString(trackRef.data(), trackRef.size(), 4u, context);
  return gid.size() == TRACK_GID_BYTES || uri.length() != 0u;
}

// r12: the retained classic queue seen on hardware may carry only TrackRef field 1
// (16-byte GID). Modern context-player JSON resolves track_uid to a Spotify URI,
// so compare both representations in one canonical identity domain. Prefer a
// native spotify:track URI when present; otherwise derive it deterministically
// from the GID. No identifier is exported by this helper.
String canonicalSpircTrackUri(const std::vector<uint8_t>& trackRef,
                              bool* hadNativeUri = nullptr, bool* hadGid = nullptr) {
  std::vector<uint8_t> gid;
  String uri;
  String context;
  if (!decodeSpircTrackRef(trackRef, gid, uri, context)) {
    if (hadNativeUri) *hadNativeUri = false;
    if (hadGid) *hadGid = false;
    return String();
  }
  const bool nativeUri = uri.startsWith(F("spotify:track:"));
  const bool validGid = gid.size() == TRACK_GID_BYTES;
  if (hadNativeUri) *hadNativeUri = nativeUri;
  if (hadGid) *hadGid = validGid;
  if (nativeUri) return uri;
  if (validGid) return spotifyTrackUriFromGid(gid.data(), gid.size());
  return String();
}

std::vector<uint8_t> buildSpircTrackRefMessage(const SpircFrameInfo& remote, const String& resolvedTrackUri) {
  std::vector<uint8_t> trackRef;
  if (remote.selectedTrackGid.size() == TRACK_GID_BYTES) {
    appendBytesField(trackRef, 1u, remote.selectedTrackGid.data(), remote.selectedTrackGid.size());
  }
  if (resolvedTrackUri.length() != 0u) appendStringField(trackRef, 2u, resolvedTrackUri);
  if (remote.contextUri.length() != 0u) appendStringField(trackRef, 4u, remote.contextUri);
  return trackRef;
}

std::vector<uint8_t> buildSpircTransferNotify(uint32_t sequence,
                                              const char* deviceId,
                                              const char* deviceName,
                                              uint16_t volume,
                                              uint64_t syncedTimestampMs,
                                              const SpircFrameInfo& remote,
                                              uint32_t forcedPlayStatus = 0xffffffffu,
                                              uint32_t durationMs = 0u) {
  (void)durationMs; // SPIRC State v2.7.1 has no duration field; keep call shape stable.
  std::vector<uint8_t> state;
  String trackUri = remote.selectedTrackUri;
  if (trackUri.length() == 0u && remote.selectedTrackGid.size() == TRACK_GID_BYTES) {
    trackUri = spotifyTrackUriFromGid(remote.selectedTrackGid.data(), remote.selectedTrackGid.size());
  }
  if (remote.contextUri.length() != 0u) appendStringField(state, 2u, remote.contextUri);

  uint32_t playingIndex = remote.hasPlayingTrackIndex ? remote.playingTrackIndex : remote.selectedTrackIndex;
  size_t emittedTrackRefs = 0u;
  if (!remote.trackRefs.empty()) {
    for (const auto& trackRef : remote.trackRefs) {
      appendMessageField(state, 27u, trackRef);
      ++emittedTrackRefs;
    }
  } else if (remote.selectedTrackGid.size() == TRACK_GID_BYTES || trackUri.length() != 0u) {
    const std::vector<uint8_t> trackRef = buildSpircTrackRefMessage(remote, trackUri);
    appendMessageField(state, 27u, trackRef);
    emittedTrackRefs = 1u;
    playingIndex = 0u;
  }
  if (emittedTrackRefs != 0u && playingIndex >= emittedTrackRefs) playingIndex = 0u;

  appendVarintField(state, 3u, playingIndex);
  const uint32_t position = remote.hasPosition ? remote.position :
                            (remote.hasPositionMs ? remote.positionMs : 0u);
  appendVarintField(state, 4u, position);
  const uint32_t playStatus = forcedPlayStatus != 0xffffffffu
                                  ? forcedPlayStatus
                                  : (remote.hasPlayStatus ? remote.playStatus : 1u);
  appendVarintField(state, 5u, playStatus);
  appendVarintField(state, 7u, syncedTimestampMs);
  appendVarintField(state, 13u, remote.hasShuffle && remote.shuffle ? 1u : 0u);
  appendVarintField(state, 14u, remote.hasRepeat && remote.repeat ? 1u : 0u);
  if (remote.hasLastCommandAck && remote.lastCommandIdent.length() != 0u) {
    appendStringField(state, 20u, remote.lastCommandIdent);
    appendVarintField(state, 21u, remote.lastCommandMsgid);
  }
  if (emittedTrackRefs != 0u) appendVarintField(state, 26u, playingIndex);

  std::vector<uint8_t> out;
  appendVarintField(out, 1u, 1u);
  appendStringField(out, 2u, String(deviceId ? deviceId : ""));
  appendStringField(out, 3u, String(SPIRC_PROTOCOL_VERSION));
  appendVarintField(out, 4u, sequence);
  appendVarintField(out, 5u, SPIRC_NOTIFY);
  appendMessageField(out, 7u, buildSpircDeviceState(deviceName, volume, true, syncedTimestampMs));
  appendMessageField(out, 12u, state);
  appendVarintField(out, 13u, position);
  appendVarintField(out, 17u, syncedTimestampMs);
  return out;
}

std::vector<uint8_t> buildMercuryRequest(
    uint64_t sequence, const String& method, const String& uri,
    const std::vector<std::vector<uint8_t>>& payloadParts = {}) {
  std::vector<uint8_t> header;
  appendStringField(header, 1u, uri);
  appendStringField(header, 3u, method);

  size_t reserve = 15u + header.size();
  for (const auto& part : payloadParts) reserve += 2u + part.size();
  std::vector<uint8_t> out;
  out.reserve(reserve);
  appendBe16(out, 8u);
  appendBe64(out, sequence);
  out.push_back(0x01u);
  appendBe16(out, static_cast<uint16_t>(1u + payloadParts.size()));
  appendBe16(out, static_cast<uint16_t>(header.size()));
  out.insert(out.end(), header.begin(), header.end());
  for (const auto& part : payloadParts) {
    if (part.size() > 0xffffu) return {};
    appendBe16(out, static_cast<uint16_t>(part.size()));
    out.insert(out.end(), part.begin(), part.end());
  }
  return out;
}

bool parseMercuryEnvelope(const std::vector<uint8_t>& data, uint64_t& sequence,
                          String& uri, String& method,
                          std::vector<std::vector<uint8_t>>& payloadParts,
                          int32_t* statusCode = nullptr) {
  payloadParts.clear();
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
  if (statusCode) {
    uint64_t rawStatus = 0u;
    *statusCode = extractProtoVarint(header, headerSize, 4u, rawStatus) ? decodeZigZag32(rawStatus) : 0;
  }
  offset += headerSize;
  for (uint16_t part = 1u; part < partCount; ++part) {
    if (offset + 2u > data.size()) return false;
    const uint16_t partSize = readBe16At(data, offset);
    offset += 2u;
    if (partSize > data.size() - offset) return false;
    payloadParts.emplace_back(data.begin() + offset, data.begin() + offset + partSize);
    offset += partSize;
  }
  return offset == data.size();
}
}

void SpotifySessionProbe::clearMetadataAudit(spotify_metadata_audit::Status status,
                                            const uint8_t* requestedGid, bool clearStatistics) {
  portENTER_CRITICAL(&metadataAuditMux_);
  keyProbe_.cancel(spotify_key_probe::Reason::TrackChanged);
  keyProbeReady_ = false;
  spotify_metadata_audit::reset(metadataAudit_);
  metadataAudit_.status = status;
  if (requestedGid) {
    memcpy(metadataAudit_.requestedGid, requestedGid, spotify_metadata_audit::kGidBytes);
    metadataAudit_.hasRequestedGid = true;
  }
  memset(&metadataAuditTarget_, 0, sizeof(metadataAuditTarget_));
  ++metadataAuditGeneration_;
  if (!metadataAuditGeneration_) ++metadataAuditGeneration_;
  if (clearStatistics) {
    metadataAuditAttempts_ = metadataAuditFailures_ = 0u;
    metadataAuditLastUs_ = metadataAuditMaxUs_ = 0u;
  }
  portEXIT_CRITICAL(&metadataAuditMux_);
}

void SpotifySessionProbe::auditMetadataPayload(const std::vector<uint8_t>& payload) {
  using namespace spotify_metadata_audit;
  const uint32_t started = micros();
  // The multi-track report must not be an automatic variable on the AP task stack.
  std::unique_ptr<Report> work(new (std::nothrow) Report);
  const bool ok = work && parse(payload.data(), payload.size(), selectedTrackGid_,
                                countryCode_, productInfoCatalogue_, *work);
  const uint32_t elapsed = micros() - started;
  portENTER_CRITICAL(&metadataAuditMux_);
  ++metadataAuditAttempts_;
  if (!ok) ++metadataAuditFailures_;
  if (work) metadataAudit_ = *work;
  else metadataAudit_.status = Status::NoMemory;
  metadataAuditLastUs_ = elapsed;
  if (elapsed > metadataAuditMaxUs_) metadataAuditMaxUs_ = elapsed;
  portEXIT_CRITICAL(&metadataAuditMux_);
  // Diagnostic failure is isolated: no mutation of normal metadata/RequestKey flow.
}

void SpotifySessionProbe::metadataAuditHttpError() {
  portENTER_CRITICAL(&metadataAuditMux_);
  metadataAudit_.status = spotify_metadata_audit::Status::HttpError;
  portEXIT_CRITICAL(&metadataAuditMux_);
}

void SpotifySessionProbe::recordMetadataKeyTarget(const uint8_t* gid, const uint8_t* file,
                                                 uint32_t sequence, bool sent) {
  portENTER_CRITICAL(&metadataAuditMux_);
  memcpy(metadataAuditTarget_.gid, gid, spotify_metadata_audit::kGidBytes);
  memcpy(metadataAuditTarget_.file, file, spotify_metadata_audit::kFileIdBytes);
  metadataAuditTarget_.pair = spotify_metadata_audit::comparePrimaryPair(metadataAudit_, gid, file);
  metadataAuditTarget_.present = true;
  metadataAuditTarget_.sent = sent;
  metadataAuditTarget_.sequence = sequence;
  portEXIT_CRITICAL(&metadataAuditMux_);
}

void SpotifySessionProbe::metadataAuditSummary(spotify_metadata_audit::Summary& out) const {
  portENTER_CRITICAL(&metadataAuditMux_);
  memset(&out, 0, sizeof(out));
  out.status = metadataAudit_.status;
  out.generation = metadataAuditGeneration_;
  out.attempts = metadataAuditAttempts_;
  out.failures = metadataAuditFailures_;
  out.lastUs = metadataAuditLastUs_;
  out.maxUs = metadataAuditMaxUs_;
  memcpy(out.country, metadataAudit_.country, sizeof(out.country));
  memcpy(out.catalogue, metadataAudit_.catalogue, sizeof(out.catalogue));
  memcpy(out.requestedGid, metadataAudit_.requestedGid, sizeof(out.requestedGid));
  out.hasRequestedGid = metadataAudit_.hasRequestedGid;
  spotify_metadata_audit::view(metadataAudit_.primary, out.primary);
  out.alternativesSeen = metadataAudit_.alternativesSeen;
  out.alternativesStored = metadataAudit_.alternativesStored;
  out.alternativesTruncated = metadataAudit_.alternativesTruncated;
  out.inputBytes = metadataAudit_.inputBytes;
  out.fieldsRead = metadataAudit_.fieldsRead;
  portEXIT_CRITICAL(&metadataAuditMux_);
}

bool SpotifySessionProbe::metadataAuditAlternative(uint32_t generation, uint8_t index,
                                                  spotify_metadata_audit::TrackView& out) const {
  portENTER_CRITICAL(&metadataAuditMux_);
  const bool valid = generation == metadataAuditGeneration_ && index < metadataAudit_.alternativesStored;
  if (valid) spotify_metadata_audit::view(metadataAudit_.alternatives[index], out);
  portEXIT_CRITICAL(&metadataAuditMux_);
  return valid;
}

bool SpotifySessionProbe::metadataAuditKeyTarget(uint32_t generation,
                                                spotify_metadata_audit::KeyTarget& out) const {
  portENTER_CRITICAL(&metadataAuditMux_);
  const bool valid = generation == metadataAuditGeneration_;
  if (valid) out = metadataAuditTarget_;
  portEXIT_CRITICAL(&metadataAuditMux_);
  return valid;
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
  clearApContinuousTrackState();
  clearApExtendedTrackState();
}

uint64_t SpotifySessionProbe::clientSpotifyVersion() const { return SPOTIFY_VERSION; }
uint8_t SpotifySessionProbe::clientProductClass() const { return CLIENT_PRODUCT_CLASS; }
uint8_t SpotifySessionProbe::clientPlatformClass() const { return CLIENT_PLATFORM_CLASS; }
uint8_t SpotifySessionProbe::authCpuClass() const { return AUTH_CPU_CLASS; }
uint8_t SpotifySessionProbe::authOsClass() const { return AUTH_OS_CLASS; }
const char* SpotifySessionProbe::authSystemName() const { return AUTH_SYSTEM_NAME; }
const char* SpotifySessionProbe::authClientVersion() const { return AUTH_CLIENT_VERSION; }

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
    case State::SpircAdvertising: return "spirc-advertising";
    case State::SpircReady: return "spirc-ready";
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

void SpotifySessionProbe::setMediaHeadError(const char* text) {
  strlcpy(mediaHeadLastError_, text ? text : "unknown", sizeof(mediaHeadLastError_));
}

void SpotifySessionProbe::setApStreamError(const char* text) {
  strlcpy(apStreamLastError_, text ? text : "unknown", sizeof(apStreamLastError_));
}

void SpotifySessionProbe::setApContinuousError(const char* text) {
  strlcpy(apContinuousLastError_, text ? text : "unknown", sizeof(apContinuousLastError_));
}

void SpotifySessionProbe::clearApContinuousTrackState() {
  apContinuousChannelId_ = 0u;
  apContinuousLastCompletedChannelId_ = 0xffffu;
  memset(apContinuousTrackGid_, 0, sizeof(apContinuousTrackGid_));
  apContinuousRangeIndex_ = 0u;
  apContinuousCompletedRanges_ = 0u;
  apContinuousRequestedAtMs_ = 0u;
  apContinuousRequestBytes_ = 0u;
  apContinuousResponsePackets_ = 0u;
  apContinuousLastCommand_ = 0u;
  apContinuousFailureCode_ = 0u;
  apContinuousHeaderCount_ = 0u;
  apContinuousHeaderBytes_ = 0u;
  apContinuousReportedFileBytes_ = 0u;
  apContinuousDataPackets_ = 0u;
  apContinuousDataBytes_ = 0u;
  apContinuousCurrentDataBytes_ = 0u;
  apContinuousProducerHash_ = 2166136261u;
  apContinuousConsumerHash_ = 2166136261u;
  apContinuousConsumerBytes_ = 0u;
  apContinuousConsumerReads_ = 0u;
  apContinuousHashMatch_ = false;
  apContinuousEof_ = false;
  apContinuousCandidateFormat_ = -1;
  apContinuousHeadersComplete_ = false;
  apContinuousPending_ = false;
  apContinuousAttemptedForTrack_ = false;
  apContinuousComplete_ = false;
  apContinuousRing_.reset();
  setApContinuousError("none");
}

void SpotifySessionProbe::setApExtendedError(const char* text) {
  strlcpy(apExtendedLastError_, text ? text : "unknown", sizeof(apExtendedLastError_));
}

void SpotifySessionProbe::snapshotApExtendedSustainedRing() {
  apExtendedSustainedRingHighWaterBytes_ = apExtendedRing_.highWaterBytes();
  apExtendedSustainedRingProducedBytes_ = apExtendedRing_.producedBytes();
  apExtendedSustainedRingConsumedBytes_ = apExtendedRing_.consumedBytes();
  apExtendedSustainedRingBackpressure_ = apExtendedRing_.backpressureEvents();
  apExtendedSustainedRingGapErrors_ = apExtendedRing_.gapErrors();
  apExtendedSustainedRingDuplicateErrors_ = apExtendedRing_.duplicateErrors();
  apExtendedSustainedRingProducerErrors_ = apExtendedRing_.producerErrors();
  apExtendedSustainedRingWriteWraps_ = apExtendedRing_.writeWraps();
  apExtendedSustainedRingReadWraps_ = apExtendedRing_.readWraps();
}

void SpotifySessionProbe::clearApExtendedTrackState() {
  apExtendedChannelId_ = 0u;
  apExtendedLastCompletedChannelId_ = 0xffffu;
  apExtendedFirstChannelId_ = 0u;
  apExtendedAllocatedChannels_ = 0u;
  memset(apExtendedTrackGid_, 0, sizeof(apExtendedTrackGid_));
  apExtendedStage_ = 0u;
  apExtendedSustainedRangeIndex_ = 0u;
  apExtendedSustainedCompletedRanges_ = 0u;
  apExtendedRequestedAtMs_ = 0u;
  apExtendedRequestBytes_ = 0u;
  apExtendedResponsePackets_ = 0u;
  apExtendedLastCommand_ = 0u;
  apExtendedFailureCode_ = 0u;
  apExtendedHeaderCount_ = 0u;
  apExtendedHeaderBytes_ = 0u;
  apExtendedReportedFileBytes_ = 0u;
  apExtendedCurrentDataBytes_ = 0u;
  apExtendedHeadersComplete_ = false;
  apExtendedPending_ = false;
  apExtendedAttemptedForTrack_ = false;
  apExtendedComplete_ = false;
  apExtendedRing_.reset();

  apExtendedSustainedDataBytes_ = 0u;
  apExtendedSustainedProducerHash_ = 2166136261u;
  apExtendedSustainedConsumerHash_ = 2166136261u;
  apExtendedSustainedConsumerBytes_ = 0u;
  apExtendedSustainedConsumerReads_ = 0u;
  apExtendedSustainedHashMatch_ = false;
  apExtendedSustainedEof_ = false;
  apExtendedSustainedRingHighWaterBytes_ = 0u;
  apExtendedSustainedRingProducedBytes_ = 0u;
  apExtendedSustainedRingConsumedBytes_ = 0u;
  apExtendedSustainedRingBackpressure_ = 0u;
  apExtendedSustainedRingGapErrors_ = 0u;
  apExtendedSustainedRingDuplicateErrors_ = 0u;
  apExtendedSustainedRingProducerErrors_ = 0u;
  apExtendedSustainedRingWriteWraps_ = 0u;
  apExtendedSustainedRingReadWraps_ = 0u;

  apExtendedTailStartBytes_ = 0u;
  apExtendedTailTargetBytes_ = 0u;
  apExtendedTailDataBytes_ = 0u;
  apExtendedTailProducerHash_ = 2166136261u;
  apExtendedTailConsumerHash_ = 2166136261u;
  apExtendedTailConsumerBytes_ = 0u;
  apExtendedTailConsumerReads_ = 0u;
  apExtendedTailHashMatch_ = false;
  apExtendedTailEof_ = false;
  apExtendedTailExactBoundary_ = false;
  setApExtendedError("none");
}

bool SpotifySessionProbe::extractXmlTag(const std::vector<uint8_t>& payload,
                                        const char* tag, String& value) {
  value = String();
  if (!tag || !*tag || payload.empty()) return false;

  const size_t tagLen = strlen(tag);
  const String closeTag = String(F("</")) + tag + '>';
  auto findNeedle = [&payload](const String& needle, size_t start) -> int32_t {
    const size_t needleLen = needle.length();
    if (needleLen == 0u || start > payload.size() || needleLen > payload.size() - start) return -1;
    const char* raw = needle.c_str();
    for (size_t i = start; i + needleLen <= payload.size(); ++i) {
      if (memcmp(payload.data() + i, raw, needleLen) == 0) return static_cast<int32_t>(i);
    }
    return -1;
  };

  // Match <tag> as well as <tag attr=...>. quick_xml used by current
  // librespot accepts both forms; dev.2j-r1 only matched the first form.
  size_t openAt = payload.size();
  size_t valueStart = payload.size();
  for (size_t i = 0u; i + tagLen + 2u <= payload.size(); ++i) {
    if (payload[i] != '<' || memcmp(payload.data() + i + 1u, tag, tagLen) != 0) continue;
    const size_t afterName = i + 1u + tagLen;
    const uint8_t delimiter = payload[afterName];
    if (delimiter != '>' && delimiter != ' ' && delimiter != '\t' &&
        delimiter != '\r' && delimiter != '\n') continue;
    size_t gt = afterName;
    while (gt < payload.size() && payload[gt] != '>') ++gt;
    if (gt >= payload.size()) return false;
    openAt = i;
    valueStart = gt + 1u;
    break;
  }
  if (openAt == payload.size() || valueStart > payload.size()) return false;

  const int32_t closeAt = findNeedle(closeTag, valueStart);
  if (closeAt < 0 || static_cast<size_t>(closeAt) < valueStart) return false;
  const size_t valueLen = static_cast<size_t>(closeAt) - valueStart;
  if (valueLen >= 256u) return false;

  value.reserve(static_cast<unsigned int>(valueLen));
  for (size_t i = 0u; i < valueLen; ++i) {
    const uint8_t byte = payload[valueStart + i];
    if (byte == 0u) return false;
    value += static_cast<char>(byte);
  }
  value.replace("&amp;", "&");
  return true;
}

bool SpotifySessionProbe::fetchMediaHeadCandidate(uint8_t candidateIndex) {
  if (mediaHeadFetchedForTrack_) return mediaHeadSuccesses_ != 0u && mediaHeadBytes_ != 0u;
  mediaHeadHttpCode_ = 0;
  mediaHeadContentLength_ = -1;
  mediaHeadBytes_ = 0u;
  mediaHeadRangeHonored_ = false;
  mediaHeadOggCapture_ = false;
  setMediaHeadError("none");

  if (candidateIndex >= audioKeyCandidateCount_ || candidateIndex >= MAX_AUDIO_KEY_CANDIDATES) {
    ++mediaHeadSkipped_;
    setMediaHeadError("no valid preferred audio file candidate");
    return false;
  }
  if (headFileTemplate_[0] == '\0') {
    ++mediaHeadSkipped_;
    setMediaHeadError("ProductInfo head-files-url missing");
    return false;
  }

  mediaHeadFetchedForTrack_ = true;

  const String headTemplate(headFileTemplate_);
  if (headTemplate.startsWith(F("https://"))) {
    ++mediaHeadUnsupportedScheme_;
    ++mediaHeadSkipped_;
    setMediaHeadError("HTTPS head-files-url unsupported by this gate");
    return false;
  }
  if (!headTemplate.startsWith(F("http://"))) {
    ++mediaHeadUnsupportedScheme_;
    ++mediaHeadSkipped_;
    setMediaHeadError("head-files-url scheme unsupported");
    return false;
  }

  const int placeholder = headTemplate.indexOf("{file_id}");
  if (placeholder < 0) {
    ++mediaHeadSkipped_;
    setMediaHeadError("head-files-url has no file_id placeholder");
    return false;
  }

  String url = headTemplate;
  const String fileIdHex = bytesToHex(audioKeyCandidateFileIds_[candidateIndex], AUDIO_FILE_ID_BYTES);
  url.replace("{file_id}", fileIdHex);
  if (url.length() == 0u || url.length() > 255u) {
    ++mediaHeadSkipped_;
    setMediaHeadError("expanded head-files-url invalid");
    return false;
  }

  ++mediaHeadAttempts_;
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(MEDIA_HEAD_TIMEOUT_MS);
  http.setTimeout(MEDIA_HEAD_TIMEOUT_MS);
  http.setReuse(false);
  if (!http.begin(client, url)) {
    setMediaHeadError("HTTP begin failed");
    return false;
  }

  const String range = String(F("bytes=0-")) + String(MEDIA_HEAD_MAX_BYTES - 1u);
  http.addHeader(F("Range"), range);
  const int code = http.GET();
  mediaHeadHttpCode_ = code;
  mediaHeadContentLength_ = http.getSize();
  mediaHeadRangeHonored_ = (code == HTTP_CODE_PARTIAL_CONTENT);
  if (code != HTTP_CODE_OK && code != HTTP_CODE_PARTIAL_CONTENT) {
    setMediaHeadError("head-file HTTP status not 200/206");
    http.end();
    return false;
  }

  auto* stream = http.getStreamPtr();
  if (!stream) {
    setMediaHeadError("head-file stream missing");
    http.end();
    return false;
  }

  uint8_t firstBytes[4] = {0u, 0u, 0u, 0u};
  size_t firstCount = 0u;
  uint8_t scratch[256];
  const uint32_t started = millis();
  while (mediaHeadBytes_ < MEDIA_HEAD_MAX_BYTES && millis() - started < MEDIA_HEAD_TIMEOUT_MS) {
    const int available = stream->available();
    if (available <= 0) {
      if (!stream->connected()) break;
      delay(1u);
      continue;
    }
    size_t want = static_cast<size_t>(available);
    if (want > sizeof(scratch)) want = sizeof(scratch);
    const size_t remaining = MEDIA_HEAD_MAX_BYTES - mediaHeadBytes_;
    if (want > remaining) want = remaining;
    const int got = stream->read(scratch, want);
    if (got <= 0) break;
    const size_t gotSize = static_cast<size_t>(got);
    for (size_t i = 0u; i < gotSize && firstCount < sizeof(firstBytes); ++i) {
      firstBytes[firstCount++] = scratch[i];
    }
    mediaHeadBytes_ += gotSize;
  }
  http.end();

  if (firstCount == sizeof(firstBytes) &&
      firstBytes[0] == 'O' && firstBytes[1] == 'g' && firstBytes[2] == 'g' && firstBytes[3] == 'S') {
    mediaHeadOggCapture_ = true;
  }
  if (mediaHeadBytes_ == 0u) {
    setMediaHeadError("head-file returned no body bytes");
    return false;
  }

  ++mediaHeadSuccesses_;
  setMediaHeadError("none");
  return true;
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
                               const std::vector<uint8_t>& authData, const char* deviceId,
                               const char* deviceName, uint8_t volumePercent) {
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
  if (active() || autoAttempted_ || state_ == State::SessionActive ||
      state_ == State::MercurySubscribing || state_ == State::SpircAdvertising ||
      state_ == State::SpircReady) return;

  const uint32_t now = millis();
  if (eligibleSinceMs_ == 0u) {
    eligibleSinceMs_ = now;
    state_ = State::Scheduled;
    return;
  }
  if (now - eligibleSinceMs_ >= AUTO_DELAY_MS) {
    autoAttempted_ = true;
    startNow(userName, authType, authData, deviceId, deviceName, volumePercent);
  }
}

bool SpotifySessionProbe::startNow(const String& userName, uint8_t authType,
                                   const std::vector<uint8_t>& authData, const char* deviceId,
                                   const char* deviceName, uint8_t volumePercent) {
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

  clearMetadataAudit(spotify_metadata_audit::Status::Idle, nullptr, true);
  credentialUser_ = userName;
  credentialAuthType_ = authType;
  credentialAuthData_ = authData;
  strlcpy(credentialDeviceId_, deviceId, sizeof(credentialDeviceId_));
  strlcpy(credentialDeviceName_, (deviceName && *deviceName) ? deviceName : "WLED Matrix",
          sizeof(credentialDeviceName_));
  if (volumePercent > 100u) volumePercent = 100u;
  credentialVolume16_ = static_cast<uint16_t>((static_cast<uint32_t>(volumePercent) * 65535u + 50u) / 100u);

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
  strlcpy(lastReadStage_, "none", sizeof(lastReadStage_));
  lastReadDeclaredPayload_ = 0u;
  sessionConnectedMs_ = 0u;
  spircHelloMercurySequence_ = ~static_cast<uint64_t>(0);
  spircTransferNotifyMercurySequence_ = ~static_cast<uint64_t>(0);
  spircBlockedNotifyMercurySequence_ = ~static_cast<uint64_t>(0);
  spircControlNotifyMercurySequence_ = ~static_cast<uint64_t>(0);
  memset(spircLastCommandIdent_, 0, sizeof(spircLastCommandIdent_));
  spircLastCommandMsgid_ = 0u;
  spircHaveCommandAck_ = false;
  spircPlaySelectFrames_ = 0u;
  spircPlaySelectByIndex_ = 0u;
  spircReplaceFrames_ = 0u;
  spircContextPlayerFrames_ = 0u;
  spircContextPlayerBytes_ = 0u;
  spircContextPlayerPlay_ = 0u;
  spircContextPlayerSelect_ = 0u;
  spircContextPlayerByUid_ = 0u;
  spircContextPlayerByUri_ = 0u;
  spircContextPlayerByIndex_ = 0u;
  spircContextPlayerByScan_ = 0u;
  spircContextPlayerByInflate_ = 0u;
  spircContextPlayerBySkipScan_ = 0u;
  spircContextPlayerUnresolved_ = 0u;
  spircContextInflateAttempts_ = 0u;
  spircContextInflateOk_ = 0u;
  spircContextInflateFailures_ = 0u;
  spircContextInflateBytes_ = 0u;
  spircContextInflatePrintablePct_ = 0u;
  spircContextSkipObjects_ = 0u;
  spircContextSkipUidCandidates_ = 0u;
  spircContextSkipUriCandidates_ = 0u;
  spircContextSkipIndexCandidates_ = 0u;
  spircContextSkipUidResolved_ = 0u;
  spircContextSkipQueueResolved_ = 0u;
  spircContextSkipAmbiguous_ = 0u;
  spircContextSkipUniqueIndex_ = -1;
  spircContextResolveLastUs_ = 0u;
  spircContextResolveMaxUs_ = 0u;
  spircSelectionApplyLastUs_ = 0u;
  spircSelectionApplyMaxUs_ = 0u;
  spircContextPlayerLastBytes_ = 0u;
  spircContextPlayerLastHash_ = 0u;
  spircContextPlayerPrintablePct_ = 0u;
  spircContextPlayerProtoValid_ = false;
  spircContextPlayerProtoFields_ = 0u;
  spircContextPlayerProtoLengthFields_ = 0u;
  spircContextPlayerQueueMatches_ = 0u;
  spircContextPlayerNonCurrentMatches_ = 0u;
  spircContextPlayerUniqueIndex_ = -1;
  spircDuplicateLoadsWithUnknownContext_ = 0u;
  spircContextPlayerEndpoint_[0] = '\0';
  spircContextPlayerEncoding_[0] = '\0';
  spircContextPlayerPrefix_[0] = '\0';
  spircContextPlayerMagic_[0] = '\0';
  spircContextPlayerProtoMap_[0] = '\0';
  spircContextInflateStatus_[0] = '\0';
  spircContextInflateEncoding_[0] = '\0';
  spircPlaybackClockRunning_ = false;
  spircPlaybackClockBasePositionMs_ = 0u;
  spircPlaybackClockStartedAtMs_ = 0u;
  strlcpy(spircPlaybackPositionSource_, "none", sizeof(spircPlaybackPositionSource_));
  spircPlaybackTrackResets_ = 0u;
  spircVirtualEosEvents_ = 0u;
  spircAutoAdvanceAttempts_ = 0u;
  spircAutoAdvanceSuccesses_ = 0u;
  spircAutoAdvanceBoundaryHolds_ = 0u;
  spircAutoAdvanceRepeatHolds_ = 0u;
  spircEosHandledGeneration_ = 0u;
  metadataPlaybackGeneration_ = 0u;
  strlcpy(spircLastEosAction_, "none", sizeof(spircLastEosAction_));
  metadataMercurySequence_ = ~static_cast<uint64_t>(0);
  metadataLastStatus_ = 0;
  metadataLastBytes_ = 0u;
  metadataRequestedAtMs_ = 0u;
  metadataLastRoundTripMs_ = 0u;
  metadataMaxRoundTripMs_ = 0u;
  audioKeyPending_ = false;
  audioKeyPendingSequence_ = 0u;
  audioKeyRequestedAtMs_ = 0u;
  memset(selectedAudioFileId_, 0, sizeof(selectedAudioFileId_));
  memset(audioKey_, 0, sizeof(audioKey_));
  audioKeyBytes_ = 0u;
  audioKeyLastCommand_ = 0u;
  audioKeyError0_ = 0u;
  audioKeyError1_ = 0u;
  audioKeyRequestBytes_ = 0u;
  audioKeyBytes_ = 0u;
  audioKeyLastCommand_ = 0u;
  audioKeyError0_ = 0u;
  audioKeyError1_ = 0u;
  memset(audioKeyCandidateFileIds_, 0, sizeof(audioKeyCandidateFileIds_));
  for (uint8_t i = 0u; i < MAX_AUDIO_KEY_CANDIDATES; ++i) audioKeyCandidateFormats_[i] = -1;
  memset(audioKeyCandidateResultCommand_, 0, sizeof(audioKeyCandidateResultCommand_));
  memset(audioKeyCandidateError0_, 0, sizeof(audioKeyCandidateError0_));
  memset(audioKeyCandidateError1_, 0, sizeof(audioKeyCandidateError1_));
  memset(audioKeyCandidateTimedOut_, 0, sizeof(audioKeyCandidateTimedOut_));
  audioKeyCandidateCount_ = 0u;
  audioKeyCandidateIndex_ = 0u;
  audioKeyCandidateAdvances_ = 0u;
  audioKeyCandidateTruncated_ = 0u;
  audioKeyServiceRejects_ = 0u;
  audioKeyProtocolErrors_ = 0u;
  audioKeyStaleResponses_ = 0u;
  audioKeyTrackChangeCancels_ = 0u;
  mediaKeyServiceBlocked_ = false;
  mediaKeyBlockEvents_ = 0u;
  mediaKeySuppressedTracks_ = 0u;
  mediaKeyBlockError0_ = 0u;
  mediaKeyBlockError1_ = 0u;
  spircStateTrackRefs_.clear();
  spircStateTrackRefBytes_ = 0u;
  spircStateTrackRefsTruncated_ = 0u;
  spircStateFallbackTrackRefs_ = 0u;
  spircStateShuffle_ = false;
  spircStateHasShuffle_ = false;
  spircStateRepeat_ = false;
  spircStateHasRepeat_ = false;
  memset(audioKey_, 0, sizeof(audioKey_));
  productInfoBytes_ = 0u;
  productInfoHash_ = 0u;
  productInfoXmlLike_ = false;
  strlcpy(productInfoType_, "none", sizeof(productInfoType_));
  strlcpy(productInfoCatalogue_, "none", sizeof(productInfoCatalogue_));
  strlcpy(productInfoPlayerLicense_, "none", sizeof(productInfoPlayerLicense_));
  strlcpy(productInfoHeadFiles_, "none", sizeof(productInfoHeadFiles_));
  strlcpy(productInfoOnDemand_, "none", sizeof(productInfoOnDemand_));
  strlcpy(productInfoHighBitrate_, "none", sizeof(productInfoHighBitrate_));
  strlcpy(productInfoUnrestricted_, "none", sizeof(productInfoUnrestricted_));
  strlcpy(productInfoMobile_, "none", sizeof(productInfoMobile_));
  strlcpy(productInfoPrefetchKeys_, "none", sizeof(productInfoPrefetchKeys_));
  strlcpy(productInfoKeyMemoryCacheMode_, "none", sizeof(productInfoKeyMemoryCacheMode_));
  strlcpy(productInfoKeyCachingMaxCount_, "none", sizeof(productInfoKeyCachingMaxCount_));
  memset(headFileTemplate_, 0, sizeof(headFileTemplate_));
  strlcpy(headFileScheme_, "none", sizeof(headFileScheme_));
  mediaHeadHttpCode_ = 0;
  mediaHeadContentLength_ = -1;
  mediaHeadBytes_ = 0u;
  mediaHeadRangeHonored_ = false;
  mediaHeadOggCapture_ = false;
  mediaHeadFetchedForTrack_ = false;
  setMediaHeadError("none");
  apStreamChannelId_ = 0u;
  apStreamLastCompletedChannelId_ = 0xffffu;
  apStreamProbeIndex_ = 0u;
  apStreamCompletedProbes_ = 0u;
  apStreamRequestedAtMs_ = 0u;
  apStreamRequestBytes_ = 0u;
  apStreamResponsePackets_ = 0u;
  apStreamLastCommand_ = 0u;
  apStreamFailureCode_ = 0u;
  apStreamHeaderCount_ = 0u;
  apStreamHeaderBytes_ = 0u;
  apStreamReportedFileBytes_ = 0u;
  apStreamDataPackets_ = 0u;
  apStreamDataBytes_ = 0u;
  apStreamCurrentDataBytes_ = 0u;
  apStreamCandidateFormat_ = -1;
  apStreamHeadersComplete_ = false;
  apStreamPending_ = false;
  apStreamAttemptedForTrack_ = false;
  apStreamMediaSource_.reset();
  apStreamLiveVerifyAttempts_ = 0u;
  apStreamLiveVerifySuccesses_ = 0u;
  apStreamLiveVerifyFailures_ = 0u;
  apStreamLiveVerifyReadCalls_ = 0u;
  apStreamLiveVerifyChunks_ = 0u;
  apStreamLiveVerifyBytes_ = 0u;
  apStreamLiveVerifyHash_ = 2166136261u;
  apStreamLiveVerifyHashMatch_ = false;
  apStreamLiveVerifyEof_ = false;
  apStreamLiveVerifyRewound_ = false;
  setApStreamError("none");
  clearApContinuousTrackState();
  clearApExtendedTrackState();
  spircHelloBytes_ = 0u;
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
  closeKeyProbeSession(spotify_key_probe::Reason::SessionStop);
  state_ = State::Stopping;
}

void SpotifySessionProbe::reset() {
  if (active()) return;
  clearMetadataAudit(spotify_metadata_audit::Status::Idle, nullptr, true);
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
  serverTimestampLocalMs_ = 0u;
  memset(countryCode_, 0, sizeof(countryCode_));
  mercurySequence_ = 0u;
  mercurySubscriptionSequence_ = ~static_cast<uint64_t>(0);
  mercurySubAttempts_ = 0u;
  mercurySubResponses_ = 0u;
  mercuryResponses_ = 0u;
  mercuryEvents_ = 0u;
  mercuryLastSequence_ = 0u;
  memset(mercuryLastUri_, 0, sizeof(mercuryLastUri_));
  spircUriRootEvents_ = 0u;
  spircUriChildEvents_ = 0u;
  strlcpy(lastReadStage_, "none", sizeof(lastReadStage_));
  lastReadDeclaredPayload_ = 0u;
  oversizedPackets_ = 0u;
  spircSequence_ = 0u;
  spircHelloMercurySequence_ = ~static_cast<uint64_t>(0);
  spircHelloAttempts_ = 0u;
  spircHelloSent_ = 0u;
  spircHelloAcks_ = 0u;
  spircHelloBytes_ = 0u;
  spircRxFrames_ = 0u;
  spircRemoteFrames_ = 0u;
  spircSelfEchoes_ = 0u;
  spircNotifyFrames_ = 0u;
  spircLoadFrames_ = 0u;
  spircPlayFrames_ = 0u;
  spircPauseFrames_ = 0u;
  spircPlayPauseFrames_ = 0u;
  spircSeekFrames_ = 0u;
  spircPrevFrames_ = 0u;
  spircNextFrames_ = 0u;
  spircPlaySelectFrames_ = 0u;
  spircPlaySelectByIndex_ = 0u;
  spircReplaceFrames_ = 0u;
  spircContextPlayerFrames_ = 0u;
  spircContextPlayerBytes_ = 0u;
  spircContextPlayerPlay_ = 0u;
  spircContextPlayerSelect_ = 0u;
  spircContextPlayerByUid_ = 0u;
  spircContextPlayerByUri_ = 0u;
  spircContextPlayerByIndex_ = 0u;
  spircContextPlayerByScan_ = 0u;
  spircContextPlayerByInflate_ = 0u;
  spircContextPlayerBySkipScan_ = 0u;
  spircContextPlayerUnresolved_ = 0u;
  spircContextInflateAttempts_ = 0u;
  spircContextInflateOk_ = 0u;
  spircContextInflateFailures_ = 0u;
  spircContextInflateBytes_ = 0u;
  spircContextInflatePrintablePct_ = 0u;
  spircContextSkipObjects_ = 0u;
  spircContextSkipUidCandidates_ = 0u;
  spircContextSkipUriCandidates_ = 0u;
  spircContextSkipIndexCandidates_ = 0u;
  spircContextSkipUidResolved_ = 0u;
  spircContextSkipQueueResolved_ = 0u;
  spircContextSkipAmbiguous_ = 0u;
  spircContextSkipUniqueIndex_ = -1;
  spircContextResolveLastUs_ = 0u;
  spircContextResolveMaxUs_ = 0u;
  spircSelectionApplyLastUs_ = 0u;
  spircSelectionApplyMaxUs_ = 0u;
  spircContextPlayerLastBytes_ = 0u;
  spircContextPlayerLastHash_ = 0u;
  spircContextPlayerPrintablePct_ = 0u;
  spircContextPlayerProtoValid_ = false;
  spircContextPlayerProtoFields_ = 0u;
  spircContextPlayerProtoLengthFields_ = 0u;
  spircContextPlayerQueueMatches_ = 0u;
  spircContextPlayerNonCurrentMatches_ = 0u;
  spircContextPlayerUniqueIndex_ = -1;
  spircDuplicateLoadsWithUnknownContext_ = 0u;
  spircContextPlayerEndpoint_[0] = '\0';
  spircContextPlayerEncoding_[0] = '\0';
  spircContextPlayerPrefix_[0] = '\0';
  spircContextPlayerMagic_[0] = '\0';
  spircContextPlayerProtoMap_[0] = '\0';
  spircContextInflateStatus_[0] = '\0';
  spircContextInflateEncoding_[0] = '\0';
  spircLastType_ = 0u;
  spircRemoteActive_ = false;
  spircLocalActive_ = false;
  spircTransferNotifyMercurySequence_ = ~static_cast<uint64_t>(0);
  spircTransferNotifyAttempts_ = 0u;
  spircTransferNotifySent_ = 0u;
  spircTransferNotifyAcks_ = 0u;
  spircTransferNotifyBytes_ = 0u;
  spircBlockedNotifyMercurySequence_ = ~static_cast<uint64_t>(0);
  spircBlockedNotifyAttempts_ = 0u;
  spircBlockedNotifySent_ = 0u;
  spircBlockedNotifyAcks_ = 0u;
  spircBlockedNotifyBytes_ = 0u;
  spircEmptyLoadsIgnored_ = 0u;
  spircDuplicateLoadsIgnored_ = 0u;
  spircDuplicateLoadsAcked_ = 0u;
  spircRecipientIgnored_ = 0u;
  spircCommandAcksSent_ = 0u;
  spircControlNotifyMercurySequence_ = ~static_cast<uint64_t>(0);
  spircControlNotifySent_ = 0u;
  spircControlNotifyAcks_ = 0u;
  spircControlNotifyBytes_ = 0u;
  memset(spircLastCommandIdent_, 0, sizeof(spircLastCommandIdent_));
  spircLastCommandMsgid_ = 0u;
  spircHaveCommandAck_ = false;
  spircStateTrackRefs_.clear();
  spircStateTrackRefBytes_ = 0u;
  spircStateTrackRefsTruncated_ = 0u;
  spircStateFallbackTrackRefs_ = 0u;
  spircStateShuffle_ = false;
  spircStateHasShuffle_ = false;
  spircStateRepeat_ = false;
  spircStateHasRepeat_ = false;
  spircLastLoadTrackCount_ = 0u;
  spircLastLoadPositionMs_ = 0u;
  spircLastLoadStatus_ = 0u;
  spircPlaybackClockRunning_ = false;
  spircPlaybackClockBasePositionMs_ = 0u;
  spircPlaybackClockStartedAtMs_ = 0u;
  strlcpy(spircPlaybackPositionSource_, "none", sizeof(spircPlaybackPositionSource_));
  spircPlaybackTrackResets_ = 0u;
  spircVirtualEosEvents_ = 0u;
  spircAutoAdvanceAttempts_ = 0u;
  spircAutoAdvanceSuccesses_ = 0u;
  spircAutoAdvanceBoundaryHolds_ = 0u;
  spircAutoAdvanceRepeatHolds_ = 0u;
  spircEosHandledGeneration_ = 0u;
  metadataPlaybackGeneration_ = 0u;
  strlcpy(spircLastEosAction_, "none", sizeof(spircLastEosAction_));
  memset(spircLastLoadContext_, 0, sizeof(spircLastLoadContext_));
  memset(spircRemoteIdent_, 0, sizeof(spircRemoteIdent_));
  memset(spircRemoteName_, 0, sizeof(spircRemoteName_));
  metadataMercurySequence_ = ~static_cast<uint64_t>(0);
  trackRefIndex_ = 0u;
  memset(trackRefGidHex_, 0, sizeof(trackRefGidHex_));
  memset(trackRefUri_, 0, sizeof(trackRefUri_));
  metadataRequests_ = 0u;
  metadataResponses_ = 0u;
  metadataSuccesses_ = 0u;
  metadataParseFailures_ = 0u;
  metadataLastStatus_ = 0;
  metadataLastBytes_ = 0u;
  metadataRequestedAtMs_ = 0u;
  metadataLastRoundTripMs_ = 0u;
  metadataMaxRoundTripMs_ = 0u;
  memset(metadataTitle_, 0, sizeof(metadataTitle_));
  memset(metadataArtists_, 0, sizeof(metadataArtists_));
  memset(metadataAlbum_, 0, sizeof(metadataAlbum_));
  metadataDurationMs_ = 0u;
  metadataCoverCount_ = 0u;
  memset(metadataCoverIdHex_, 0, sizeof(metadataCoverIdHex_));
  metadataAudioFileCount_ = 0u;
  metadataPreferredFormat_ = -1;
  memset(metadataPreferredFileIdHex_, 0, sizeof(metadataPreferredFileIdHex_));
  memset(selectedTrackGid_, 0, sizeof(selectedTrackGid_));
  memset(selectedAudioFileId_, 0, sizeof(selectedAudioFileId_));
  memset(audioKey_, 0, sizeof(audioKey_));
  memset(audioKeyCandidateFileIds_, 0, sizeof(audioKeyCandidateFileIds_));
  for (uint8_t i = 0u; i < MAX_AUDIO_KEY_CANDIDATES; ++i) audioKeyCandidateFormats_[i] = -1;
  memset(audioKeyCandidateResultCommand_, 0, sizeof(audioKeyCandidateResultCommand_));
  memset(audioKeyCandidateError0_, 0, sizeof(audioKeyCandidateError0_));
  memset(audioKeyCandidateError1_, 0, sizeof(audioKeyCandidateError1_));
  memset(audioKeyCandidateTimedOut_, 0, sizeof(audioKeyCandidateTimedOut_));
  audioKeyCandidateCount_ = 0u;
  audioKeyCandidateIndex_ = 0u;
  audioKeyCandidateAdvances_ = 0u;
  audioKeyCandidateTruncated_ = 0u;
  audioKeyNextSequence_ = 0u;
  audioKeyPendingSequence_ = 0u;
  audioKeyLastSequence_ = 0u;
  audioKeyRequestedAtMs_ = 0u;
  audioKeyRequests_ = 0u;
  audioKeyResponses_ = 0u;
  audioKeySuccesses_ = 0u;
  audioKeyErrors_ = 0u;
  audioKeyTimeouts_ = 0u;
  audioKeyServiceRejects_ = 0u;
  audioKeyProtocolErrors_ = 0u;
  audioKeyStaleResponses_ = 0u;
  audioKeyTrackChangeCancels_ = 0u;
  audioKeyRequestBytes_ = 0u;
  audioKeyBytes_ = 0u;
  audioKeyLastCommand_ = 0u;
  audioKeyError0_ = 0u;
  audioKeyError1_ = 0u;
  audioKeyPending_ = false;
  mediaKeyServiceBlocked_ = false;
  mediaKeyBlockEvents_ = 0u;
  mediaKeySuppressedTracks_ = 0u;
  mediaKeyBlockError0_ = 0u;
  mediaKeyBlockError1_ = 0u;
  productInfoPackets_ = 0u;
  productInfoBytes_ = 0u;
  productInfoHash_ = 0u;
  productInfoXmlLike_ = false;
  strlcpy(productInfoType_, "none", sizeof(productInfoType_));
  strlcpy(productInfoCatalogue_, "none", sizeof(productInfoCatalogue_));
  strlcpy(productInfoPlayerLicense_, "none", sizeof(productInfoPlayerLicense_));
  strlcpy(productInfoHeadFiles_, "none", sizeof(productInfoHeadFiles_));
  strlcpy(productInfoOnDemand_, "none", sizeof(productInfoOnDemand_));
  strlcpy(productInfoHighBitrate_, "none", sizeof(productInfoHighBitrate_));
  strlcpy(productInfoUnrestricted_, "none", sizeof(productInfoUnrestricted_));
  strlcpy(productInfoMobile_, "none", sizeof(productInfoMobile_));
  strlcpy(productInfoPrefetchKeys_, "none", sizeof(productInfoPrefetchKeys_));
  strlcpy(productInfoKeyMemoryCacheMode_, "none", sizeof(productInfoKeyMemoryCacheMode_));
  strlcpy(productInfoKeyCachingMaxCount_, "none", sizeof(productInfoKeyCachingMaxCount_));
  memset(headFileTemplate_, 0, sizeof(headFileTemplate_));
  strlcpy(headFileScheme_, "none", sizeof(headFileScheme_));
  mediaHeadAttempts_ = 0u;
  mediaHeadSuccesses_ = 0u;
  mediaHeadSkipped_ = 0u;
  mediaHeadHttpCode_ = 0;
  mediaHeadContentLength_ = -1;
  mediaHeadBytes_ = 0u;
  mediaHeadRangeHonored_ = false;
  mediaHeadOggCapture_ = false;
  mediaHeadUnsupportedScheme_ = 0u;
  mediaHeadFetchedForTrack_ = false;
  setMediaHeadError("none");
  apStreamAttempts_ = 0u;
  apStreamSuccesses_ = 0u;
  apStreamFailures_ = 0u;
  apStreamTimeouts_ = 0u;
  apStreamProtocolErrors_ = 0u;
  apStreamStalePackets_ = 0u;
  apStreamPostCompletePackets_ = 0u;
  apStreamTrackChangeCancels_ = 0u;
  apStreamNextChannelId_ = 0u;
  apStreamChannelId_ = 0u;
  apStreamLastCompletedChannelId_ = 0xffffu;
  apStreamProbeIndex_ = 0u;
  apStreamCompletedProbes_ = 0u;
  apStreamRequestedAtMs_ = 0u;
  apStreamRequestBytes_ = 0u;
  apStreamResponsePackets_ = 0u;
  apStreamLastCommand_ = 0u;
  apStreamFailureCode_ = 0u;
  apStreamHeaderCount_ = 0u;
  apStreamHeaderBytes_ = 0u;
  apStreamReportedFileBytes_ = 0u;
  apStreamDataPackets_ = 0u;
  apStreamDataBytes_ = 0u;
  apStreamCurrentDataBytes_ = 0u;
  apStreamCandidateFormat_ = -1;
  apStreamHeadersComplete_ = false;
  apStreamPending_ = false;
  apStreamAttemptedForTrack_ = false;
  apStreamMediaSource_.reset();
  apStreamLiveVerifyAttempts_ = 0u;
  apStreamLiveVerifySuccesses_ = 0u;
  apStreamLiveVerifyFailures_ = 0u;
  apStreamLiveVerifyReadCalls_ = 0u;
  apStreamLiveVerifyChunks_ = 0u;
  apStreamLiveVerifyBytes_ = 0u;
  apStreamLiveVerifyHash_ = 2166136261u;
  apStreamLiveVerifyHashMatch_ = false;
  apStreamLiveVerifyEof_ = false;
  apStreamLiveVerifyRewound_ = false;
  setApStreamError("none");
  apContinuousAttempts_ = 0u;
  apContinuousSuccesses_ = 0u;
  apContinuousFailures_ = 0u;
  apContinuousTimeouts_ = 0u;
  apContinuousProtocolErrors_ = 0u;
  apContinuousStalePackets_ = 0u;
  apContinuousPostCompletePackets_ = 0u;
  apContinuousTrackChangeCancels_ = 0u;
  clearApContinuousTrackState();
  apExtendedAttempts_ = 0u;
  apExtendedSuccesses_ = 0u;
  apExtendedFailures_ = 0u;
  apExtendedTimeouts_ = 0u;
  apExtendedProtocolErrors_ = 0u;
  apExtendedStalePackets_ = 0u;
  apExtendedPostCompletePackets_ = 0u;
  apExtendedTrackChangeCancels_ = 0u;
  apExtendedLastCancelProducedBytes_ = 0u;
  apExtendedLastCancelBufferedBytes_ = 0u;
  strlcpy(apExtendedLastCancelStage_, "none", sizeof(apExtendedLastCancelStage_));
  clearApExtendedTrackState();
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
  memset(credentialDeviceName_, 0, sizeof(credentialDeviceName_));
  credentialVolume16_ = 0u;
  apContinuousRing_.reset();
  apExtendedRing_.reset();
  taskStartedMs_ = 0u;
  task_ = nullptr;
}

bool SpotifySessionProbe::runOneSession(bool reconnecting) {
  openKeyProbeSession();
  clearMetadataAudit(spotify_metadata_audit::Status::Idle);
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
  spircLocalActive_ = false;
  spircTransferNotifyMercurySequence_ = ~static_cast<uint64_t>(0);
  metadataMercurySequence_ = ~static_cast<uint64_t>(0);
  audioKeyPending_ = false;
  audioKeyPendingSequence_ = 0u;
  audioKeyRequestedAtMs_ = 0u;
  memset(audioKeyCandidateFileIds_, 0, sizeof(audioKeyCandidateFileIds_));
  for (uint8_t i = 0u; i < MAX_AUDIO_KEY_CANDIDATES; ++i) audioKeyCandidateFormats_[i] = -1;
  memset(audioKeyCandidateResultCommand_, 0, sizeof(audioKeyCandidateResultCommand_));
  memset(audioKeyCandidateError0_, 0, sizeof(audioKeyCandidateError0_));
  memset(audioKeyCandidateError1_, 0, sizeof(audioKeyCandidateError1_));
  memset(audioKeyCandidateTimedOut_, 0, sizeof(audioKeyCandidateTimedOut_));
  audioKeyCandidateCount_ = 0u;
  audioKeyCandidateIndex_ = 0u;
  audioKeyCandidateAdvances_ = 0u;
  audioKeyCandidateTruncated_ = 0u;
  productInfoBytes_ = 0u;
  productInfoHash_ = 0u;
  productInfoXmlLike_ = false;
  strlcpy(productInfoType_, "none", sizeof(productInfoType_));
  strlcpy(productInfoCatalogue_, "none", sizeof(productInfoCatalogue_));
  strlcpy(productInfoPlayerLicense_, "none", sizeof(productInfoPlayerLicense_));
  strlcpy(productInfoHeadFiles_, "none", sizeof(productInfoHeadFiles_));
  strlcpy(productInfoOnDemand_, "none", sizeof(productInfoOnDemand_));
  strlcpy(productInfoHighBitrate_, "none", sizeof(productInfoHighBitrate_));
  strlcpy(productInfoUnrestricted_, "none", sizeof(productInfoUnrestricted_));
  strlcpy(productInfoMobile_, "none", sizeof(productInfoMobile_));
  strlcpy(productInfoPrefetchKeys_, "none", sizeof(productInfoPrefetchKeys_));
  strlcpy(productInfoKeyMemoryCacheMode_, "none", sizeof(productInfoKeyMemoryCacheMode_));
  strlcpy(productInfoKeyCachingMaxCount_, "none", sizeof(productInfoKeyCachingMaxCount_));
  memset(headFileTemplate_, 0, sizeof(headFileTemplate_));
  strlcpy(headFileScheme_, "none", sizeof(headFileScheme_));
  mediaHeadHttpCode_ = 0;
  mediaHeadContentLength_ = -1;
  mediaHeadBytes_ = 0u;
  mediaHeadRangeHonored_ = false;
  mediaHeadOggCapture_ = false;
  mediaHeadFetchedForTrack_ = false;
  setMediaHeadError("none");
  apStreamChannelId_ = 0u;
  apStreamLastCompletedChannelId_ = 0xffffu;
  apStreamProbeIndex_ = 0u;
  apStreamCompletedProbes_ = 0u;
  apStreamRequestedAtMs_ = 0u;
  apStreamRequestBytes_ = 0u;
  apStreamResponsePackets_ = 0u;
  apStreamLastCommand_ = 0u;
  apStreamFailureCode_ = 0u;
  apStreamHeaderCount_ = 0u;
  apStreamHeaderBytes_ = 0u;
  apStreamReportedFileBytes_ = 0u;
  apStreamDataPackets_ = 0u;
  apStreamDataBytes_ = 0u;
  apStreamCurrentDataBytes_ = 0u;
  apStreamCandidateFormat_ = -1;
  apStreamHeadersComplete_ = false;
  apStreamPending_ = false;
  apStreamAttemptedForTrack_ = false;
  apStreamMediaSource_.reset();
  apStreamLiveVerifyAttempts_ = 0u;
  apStreamLiveVerifySuccesses_ = 0u;
  apStreamLiveVerifyFailures_ = 0u;
  apStreamLiveVerifyReadCalls_ = 0u;
  apStreamLiveVerifyChunks_ = 0u;
  apStreamLiveVerifyBytes_ = 0u;
  apStreamLiveVerifyHash_ = 2166136261u;
  apStreamLiveVerifyHashMatch_ = false;
  apStreamLiveVerifyEof_ = false;
  apStreamLiveVerifyRewound_ = false;
  setApStreamError("none");
  clearApContinuousTrackState();
  clearApExtendedTrackState();
  sessionConnectedMs_ = millis();
  lastRxMs_ = sessionConnectedMs_;
  updateStackWatermark();

  // Current embedded cspot interoperability references use
  // hm://remote/3/user/<user>/. Spotify may deliver subscribed SPIRC events on
  // descendant URIs such as hm://remote/3/user/<user>/<connection-id>; treat the
  // subscribed root and all of its descendants as the same SPIRC stream.
  const String subscriptionUri = String(F("hm://remote/3/user/")) + credentialUser_ + F("/");
  auto isSpircSubscriptionUri = [&subscriptionUri](const String& uri) -> bool {
    return uri == subscriptionUri || uri.startsWith(subscriptionUri);
  };
  bool subscriptionSent = false;
  bool subscriptionReadyThisSession = false;
  bool spircHelloSentThisSession = false;

  auto syncedTimestampMs = [this]() -> uint64_t {
    if (serverTimestampSeconds_ == 0u) return static_cast<uint64_t>(millis());
    const uint32_t elapsed = serverTimestampLocalMs_ == 0u ? 0u : millis() - serverTimestampLocalMs_;
    return static_cast<uint64_t>(serverTimestampSeconds_) * 1000ULL + elapsed;
  };

  auto sendSpircHello = [&]() -> bool {
    if (spircHelloSentThisSession) return true;
    state_ = State::SpircAdvertising;
    ++spircHelloAttempts_;
    const std::vector<uint8_t> frame = buildSpircFrame(
        SPIRC_HELLO, spircSequence_++, credentialDeviceId_, credentialDeviceName_,
        credentialVolume16_, syncedTimestampMs());
    spircHelloBytes_ = frame.size();
    spircHelloMercurySequence_ = mercurySequence_++;
    const std::vector<uint8_t> request = buildMercuryRequest(
        spircHelloMercurySequence_, String(F("SEND")), subscriptionUri, {frame});
    if (request.empty() ||
        !sendShannonPacket(tcp, sendCipher, sendNonce, MERCURY_SEND_COMMAND, request, IO_TIMEOUT_MS)) {
      setError("SPIRC Hello Mercury SEND failed");
      return false;
    }
    spircHelloSentThisSession = true;
    ++spircHelloSent_;
    ++txPackets_;
    return true;
  };

  auto currentSpircPositionMs = [&]() -> uint32_t {
    uint32_t position = spircPlaybackClockBasePositionMs_;
    if (spircPlaybackClockRunning_) {
      const uint32_t elapsed = static_cast<uint32_t>(millis() - spircPlaybackClockStartedAtMs_);
      if (UINT32_MAX - position < elapsed) position = UINT32_MAX;
      else position += elapsed;
    }
    if (metadataDurationMs_ != 0u && position > metadataDurationMs_) position = metadataDurationMs_;
    return position;
  };

  auto setSpircPlaybackClock = [&](uint32_t status, uint32_t positionMs, const char* source) {
    spircPlaybackClockBasePositionMs_ = positionMs;
    spircPlaybackClockStartedAtMs_ = millis();
    spircPlaybackClockRunning_ = status == 1u;
    if (source) strlcpy(spircPlaybackPositionSource_, source, sizeof(spircPlaybackPositionSource_));
  };

  auto sendSpircTransferNotify = [&](const SpircFrameInfo& remote, const char* positionSource) -> bool {
    ++spircTransferNotifyAttempts_;
    const std::vector<uint8_t> frame = buildSpircTransferNotify(
        spircSequence_++, credentialDeviceId_, credentialDeviceName_,
        credentialVolume16_, syncedTimestampMs(), remote);
    spircTransferNotifyBytes_ = frame.size();
    spircTransferNotifyMercurySequence_ = mercurySequence_++;
    const std::vector<uint8_t> request = buildMercuryRequest(
        spircTransferNotifyMercurySequence_, String(F("SEND")), subscriptionUri, {frame});
    if (request.empty() ||
        !sendShannonPacket(tcp, sendCipher, sendNonce, MERCURY_SEND_COMMAND, request, IO_TIMEOUT_MS)) {
      setError("SPIRC Load Notify Mercury SEND failed");
      return false;
    }
    ++spircTransferNotifySent_;
    ++txPackets_;

    // Mirror the controller's current State.track (field 27) queue for later
    // service-blocked Notify frames. Current SPIRC controllers derive the
    // playable/current-track UI from this repeated TrackRef list; a context
    // plus index without TrackRefs leaves the controller with no usable queue.
    spircStateTrackRefs_ = remote.trackRefs;
    spircStateTrackRefBytes_ = remote.trackRefBytes;
    spircStateTrackRefsTruncated_ = remote.trackRefsTruncated;
    spircStateShuffle_ = remote.shuffle;
    spircStateHasShuffle_ = remote.hasShuffle;
    spircStateRepeat_ = remote.repeat;
    spircStateHasRepeat_ = remote.hasRepeat;
    if (remote.hasLastCommandAck) {
      strlcpy(spircLastCommandIdent_, remote.lastCommandIdent.c_str(), sizeof(spircLastCommandIdent_));
      spircLastCommandMsgid_ = remote.lastCommandMsgid;
      spircHaveCommandAck_ = true;
      ++spircCommandAcksSent_;
    }
    if (spircStateTrackRefs_.empty() &&
        (remote.selectedTrackGid.size() == TRACK_GID_BYTES || remote.selectedTrackUri.length() != 0u)) {
      String uri = remote.selectedTrackUri;
      if (uri.length() == 0u && remote.selectedTrackGid.size() == TRACK_GID_BYTES) {
        uri = spotifyTrackUriFromGid(remote.selectedTrackGid.data(), remote.selectedTrackGid.size());
      }
      std::vector<uint8_t> fallback = buildSpircTrackRefMessage(remote, uri);
      if (!fallback.empty()) {
        spircStateTrackRefBytes_ = fallback.size();
        spircStateTrackRefs_.push_back(std::move(fallback));
        ++spircStateFallbackTrackRefs_;
      }
    }

    spircLocalActive_ = true;
    spircLastLoadTrackCount_ = remote.trackCount;
    spircLastLoadPositionMs_ = remote.hasPosition ? remote.position :
                               (remote.hasPositionMs ? remote.positionMs : 0u);
    spircLastLoadStatus_ = remote.hasPlayStatus ? remote.playStatus : 0u;
    setSpircPlaybackClock(spircLastLoadStatus_, spircLastLoadPositionMs_, positionSource);
    if (positionSource && strcmp(positionSource, "reset-track-change") == 0) ++spircPlaybackTrackResets_;
    strlcpy(spircLastLoadContext_, remote.contextUri.c_str(), sizeof(spircLastLoadContext_));
    state_ = State::SpircReady;
    return true;
  };

  auto sendSpircBlockedNotify = [&]() -> bool {
    if (trackRefGidHex_[0] == '\0') return true;
    ++spircBlockedNotifyAttempts_;
    SpircFrameInfo current;
    current.contextUri = String(spircLastLoadContext_);
    current.positionMs = currentSpircPositionMs();
    current.hasPositionMs = true;
    current.playStatus = (spircLastLoadStatus_ == 1u) ? 1u : 2u;
    current.hasPlayStatus = true;
    current.playingTrackIndex = trackRefIndex_;
    current.hasPlayingTrackIndex = true;
    current.selectedTrackIndex = trackRefIndex_;
    current.selectedTrackGid.assign(selectedTrackGid_, selectedTrackGid_ + TRACK_GID_BYTES);
    current.selectedTrackUri = String(trackRefUri_);
    current.trackRefs = spircStateTrackRefs_;
    current.trackRefBytes = spircStateTrackRefBytes_;
    current.trackRefsTruncated = spircStateTrackRefsTruncated_;
    current.shuffle = spircStateShuffle_;
    current.hasShuffle = spircStateHasShuffle_;
    current.repeat = spircStateRepeat_;
    current.hasRepeat = spircStateHasRepeat_;
    if (spircHaveCommandAck_) {
      current.lastCommandIdent = String(spircLastCommandIdent_);
      current.lastCommandMsgid = spircLastCommandMsgid_;
      current.hasLastCommandAck = true;
    }
    const std::vector<uint8_t> frame = buildSpircTransferNotify(
        spircSequence_++, credentialDeviceId_, credentialDeviceName_,
        credentialVolume16_, syncedTimestampMs(), current, current.playStatus, metadataDurationMs_);
    spircBlockedNotifyBytes_ = frame.size();
    spircBlockedNotifyMercurySequence_ = mercurySequence_++;
    const std::vector<uint8_t> request = buildMercuryRequest(
        spircBlockedNotifyMercurySequence_, String(F("SEND")), subscriptionUri, {frame});
    if (request.empty() ||
        !sendShannonPacket(tcp, sendCipher, sendNonce, MERCURY_SEND_COMMAND, request, IO_TIMEOUT_MS)) {
      setError("SPIRC blocked-state Notify Mercury SEND failed");
      return false;
    }
    ++spircBlockedNotifySent_;
    ++txPackets_;
    spircLocalActive_ = true;
    spircLastLoadStatus_ = current.playStatus;
    spircLastLoadPositionMs_ = current.positionMs;
    setSpircPlaybackClock(current.playStatus, current.positionMs, nullptr);
    state_ = State::SpircReady;
    return true;
  };

  auto rememberSpircCommandAck = [&](const SpircFrameInfo& command) {
    if (!command.hasLastCommandAck) return;
    strlcpy(spircLastCommandIdent_, command.lastCommandIdent.c_str(), sizeof(spircLastCommandIdent_));
    spircLastCommandMsgid_ = command.lastCommandMsgid;
    spircHaveCommandAck_ = true;
  };

  auto makeRetainedSpircState = [&](uint32_t index, uint32_t status, uint32_t positionMs) {
    SpircFrameInfo current;
    current.contextUri = String(spircLastLoadContext_);
    current.positionMs = positionMs;
    current.hasPositionMs = true;
    current.playStatus = status;
    current.hasPlayStatus = true;
    current.playingTrackIndex = index;
    current.hasPlayingTrackIndex = true;
    current.selectedTrackIndex = index;
    current.trackRefs = spircStateTrackRefs_;
    current.trackRefBytes = spircStateTrackRefBytes_;
    current.trackRefsTruncated = spircStateTrackRefsTruncated_;
    current.shuffle = spircStateShuffle_;
    current.hasShuffle = spircStateHasShuffle_;
    current.repeat = spircStateRepeat_;
    current.hasRepeat = spircStateHasRepeat_;
    if (index < spircStateTrackRefs_.size()) {
      String refContext;
      decodeSpircTrackRef(spircStateTrackRefs_[index], current.selectedTrackGid,
                          current.selectedTrackUri, refContext);
      if (current.contextUri.length() == 0u && refContext.length() != 0u) current.contextUri = refContext;
    } else if (trackRefGidHex_[0] != '\0') {
      current.selectedTrackGid.assign(selectedTrackGid_, selectedTrackGid_ + TRACK_GID_BYTES);
      current.selectedTrackUri = String(trackRefUri_);
      current.selectedTrackIndex = trackRefIndex_;
      current.playingTrackIndex = trackRefIndex_;
    }
    if (spircHaveCommandAck_) {
      current.lastCommandIdent = String(spircLastCommandIdent_);
      current.lastCommandMsgid = spircLastCommandMsgid_;
      current.hasLastCommandAck = true;
    }
    return current;
  };


  auto resolveRetainedIndexByUri = [&](const String& uri, uint32_t& outIndex) -> bool {
    if (!uri.startsWith(F("spotify:track:"))) return false;
    bool found = false;
    uint32_t matchedIndex = 0u;
    for (size_t i = 0u; i < spircStateTrackRefs_.size(); ++i) {
      const String canonicalUri = canonicalSpircTrackUri(spircStateTrackRefs_[i]);
      if (canonicalUri != uri) continue;
      if (found && matchedIndex != static_cast<uint32_t>(i)) return false;
      matchedIndex = static_cast<uint32_t>(i);
      found = true;
    }
    if (found) outIndex = matchedIndex;
    return found;
  };

  auto resolveContextPlayerSelection = [&](const SpircFrameInfo& info, uint32_t& outIndex) -> bool {
    const uint32_t resolveStartedUs = micros();
    struct ResolveTimingGuard {
      uint32_t started;
      uint32_t& last;
      uint32_t& max;
      ~ResolveTimingGuard() {
        const uint32_t elapsed = micros() - started;
        last = elapsed;
        if (elapsed > max) max = elapsed;
      }
    } resolveTiming{resolveStartedUs, spircContextResolveLastUs_, spircContextResolveMaxUs_};
    if (!info.hasContextPlayerState) return false;
    ++spircContextPlayerFrames_;
    spircContextPlayerBytes_ += info.contextPlayerStateBytes;
    spircContextPlayerLastBytes_ = info.contextPlayerStateBytes;
    spircContextPlayerLastHash_ = info.contextPlayerHash;
    spircContextPlayerPrintablePct_ = info.contextPlayerPrintablePct;
    spircContextPlayerProtoValid_ = info.contextPlayerProtoValid;
    spircContextPlayerProtoFields_ = info.contextPlayerProtoFields;
    spircContextPlayerProtoLengthFields_ = info.contextPlayerProtoLengthFields;
    spircContextPlayerQueueMatches_ = 0u;
    spircContextPlayerNonCurrentMatches_ = 0u;
    spircContextPlayerUniqueIndex_ = -1;
    spircContextQueueRefs_ = 0u;
    spircContextQueueGidOnly_ = 0u;
    spircContextQueueNativeUri_ = 0u;
    spircContextQueueCanonical_ = 0u;
    spircContextSkipObjects_ = 0u;
    spircContextSkipUidCandidates_ = 0u;
    spircContextSkipUriCandidates_ = 0u;
    spircContextSkipIndexCandidates_ = 0u;
    spircContextSkipUidResolved_ = 0u;
    spircContextSkipQueueResolved_ = 0u;
    spircContextSkipIndexValidated_ = 0u;
    spircContextSkipIndexIgnored_ = 0u;
    spircContextSkipAmbiguous_ = 0u;
    spircContextSkipUniqueIndex_ = -1;
    for (const auto& trackRef : spircStateTrackRefs_) {
      bool hadNativeUri = false;
      bool hadGid = false;
      const String canonicalUri = canonicalSpircTrackUri(trackRef, &hadNativeUri, &hadGid);
      ++spircContextQueueRefs_;
      if (hadNativeUri) ++spircContextQueueNativeUri_;
      if (hadGid && !hadNativeUri) ++spircContextQueueGidOnly_;
      if (canonicalUri.startsWith(F("spotify:track:"))) ++spircContextQueueCanonical_;
    }
    spircContextInflateBytes_ = 0u;
    spircContextInflatePrintablePct_ = 0u;
    spircContextInflateStatus_[0] = '\0';
    spircContextInflateEncoding_[0] = '\0';
    if (info.contextPlayerEncoding.length() != 0u) {
      strlcpy(spircContextPlayerEncoding_, info.contextPlayerEncoding.c_str(), sizeof(spircContextPlayerEncoding_));
    }
    if (info.contextPlayerPrefix.length() != 0u) {
      strlcpy(spircContextPlayerPrefix_, info.contextPlayerPrefix.c_str(), sizeof(spircContextPlayerPrefix_));
    }
    if (info.contextPlayerMagic.length() != 0u) {
      strlcpy(spircContextPlayerMagic_, info.contextPlayerMagic.c_str(), sizeof(spircContextPlayerMagic_));
    }
    if (info.contextPlayerProtoMap.length() != 0u) {
      strlcpy(spircContextPlayerProtoMap_, info.contextPlayerProtoMap.c_str(), sizeof(spircContextPlayerProtoMap_));
    } else {
      spircContextPlayerProtoMap_[0] = '\0';
    }
    if (info.contextPlayerEndpoint.length() != 0u) {
      strlcpy(spircContextPlayerEndpoint_, info.contextPlayerEndpoint.c_str(), sizeof(spircContextPlayerEndpoint_));
      if (info.contextPlayerEndpoint == F("play")) ++spircContextPlayerPlay_;
    }

    auto scanRetainedQueue = [&](const uint8_t* payload, size_t payloadSize, uint32_t& selected) -> bool {
      if (!payload || payloadSize == 0u || spircStateTrackRefs_.empty()) return false;
      uint32_t uniqueNonCurrent = 0xffffffffu;
      uint32_t nonCurrent = 0u;
      uint32_t matches = 0u;
      for (size_t i = 0u; i < spircStateTrackRefs_.size(); ++i) {
        std::vector<uint8_t> gid;
        String uri;
        String context;
        if (!decodeSpircTrackRef(spircStateTrackRefs_[i], gid, uri, context)) continue;
        const bool uriHit = uri.length() != 0u &&
            bytesContain(payload, payloadSize,
                         reinterpret_cast<const uint8_t*>(uri.c_str()), uri.length());
        const bool gidHit = gid.size() == TRACK_GID_BYTES &&
            bytesContain(payload, payloadSize, gid.data(), gid.size());
        if (!uriHit && !gidHit) continue;
        ++matches;
        if (i != trackRefIndex_) {
          ++nonCurrent;
          uniqueNonCurrent = static_cast<uint32_t>(i);
        }
      }
      spircContextPlayerQueueMatches_ = matches;
      spircContextPlayerNonCurrentMatches_ = nonCurrent;
      if (nonCurrent == 1u && uniqueNonCurrent < spircStateTrackRefs_.size()) {
        spircContextPlayerUniqueIndex_ = static_cast<int32_t>(uniqueNonCurrent);
        selected = uniqueNonCurrent;
        return true;
      }
      return false;
    };

    // r12: keep r11's bounded multi-skip enumeration, but resolve modern URI
    // identities against a canonical classic queue where GID-only TrackRefs are
    // converted to spotify:track URIs. track_index remains diagnostic/advisory:
    // it is never allowed to select a queue entry by itself.
    auto resolveJsonSkipCandidates = [&](const uint8_t* payload, size_t payloadSize,
                                         uint32_t& selected) -> bool {
      spircContextSkipObjects_ = 0u;
      spircContextSkipUidCandidates_ = 0u;
      spircContextSkipUriCandidates_ = 0u;
      spircContextSkipIndexCandidates_ = 0u;
      spircContextSkipUidResolved_ = 0u;
      spircContextSkipQueueResolved_ = 0u;
      spircContextSkipIndexValidated_ = 0u;
      spircContextSkipIndexIgnored_ = 0u;
      spircContextSkipAmbiguous_ = 0u;
      spircContextSkipUniqueIndex_ = -1;
      if (!payload || payloadSize == 0u || spircStateTrackRefs_.empty()) return false;

      bool haveUnique = false;
      uint32_t unique = 0u;
      auto consider = [&](uint32_t idx) {
        if (idx >= spircStateTrackRefs_.size() || idx == trackRefIndex_) return;
        ++spircContextSkipQueueResolved_;
        if (!haveUnique) {
          unique = idx;
          haveUnique = true;
        } else if (unique != idx) {
          ++spircContextSkipAmbiguous_;
        }
      };

      size_t search = 0u;
      while (search < payloadSize && spircContextSkipObjects_ < 16u) {
        size_t skipStart = payloadSize;
        size_t keyPos = payloadSize;
        if (!findJsonKeyValueStart(payload, payloadSize, "skip_to", skipStart,
                                   search, payloadSize, &keyPos)) break;
        ++spircContextSkipObjects_;

        size_t skipEnd = payloadSize;
        if (skipStart < payloadSize && (payload[skipStart] == '{' || payload[skipStart] == '[')) {
          if (!jsonCompositeEnd(payload, payloadSize, skipStart, skipEnd)) skipEnd = payloadSize;
        } else {
          skipEnd = std::min(payloadSize, skipStart + 2048u);
        }

        bool objectIdentityResolved = false;
        uint32_t objectIdentityIndex = 0u;
        auto rememberIdentity = [&](uint32_t idx) {
          if (!objectIdentityResolved) {
            objectIdentityIndex = idx;
            objectIdentityResolved = true;
          } else if (objectIdentityIndex != idx) {
            ++spircContextSkipAmbiguous_;
          }
          consider(idx);
        };

        String uri;
        if (extractJsonQuotedValue(payload, payloadSize, "track_uri", uri, skipStart, skipEnd)) {
          ++spircContextSkipUriCandidates_;
          uint32_t idx = 0u;
          if (uri.startsWith(F("spotify:track:")) && resolveRetainedIndexByUri(uri, idx)) rememberIdentity(idx);
        }

        String uid;
        if (extractJsonQuotedValue(payload, payloadSize, "track_uid", uid, skipStart, skipEnd)) {
          ++spircContextSkipUidCandidates_;
          String uidUri;
          if (resolveContextPlayerUidToUri(payload, payloadSize, uid, uidUri)) {
            ++spircContextSkipUidResolved_;
            uint32_t idx = 0u;
            if (resolveRetainedIndexByUri(uidUri, idx)) rememberIdentity(idx);
          }
        }

        uint32_t idxValue = 0u;
        if (extractJsonUintValue(payload, payloadSize, "track_index", idxValue, skipStart, skipEnd)) {
          ++spircContextSkipIndexCandidates_;
          if (objectIdentityResolved && idxValue == objectIdentityIndex) {
            ++spircContextSkipIndexValidated_;
          } else {
            ++spircContextSkipIndexIgnored_;
          }
        }

        const size_t next = skipEnd > keyPos ? skipEnd : keyPos + 1u;
        search = next > search ? next : search + 1u;
      }

      if (haveUnique && spircContextSkipAmbiguous_ == 0u) {
        selected = unique;
        spircContextSkipUniqueIndex_ = static_cast<int32_t>(unique);
        return true;
      }
      return false;
    };

    // Historical uncompressed JSON uses the same bounded multi-skip safety as
    // gzip JSON. Do not let parseContextPlayerState's first skip_to shortcut
    // bypass convergence across all explicit targets.
    if (info.contextPlayerEncoding == F("json") && !info.contextPlayerState.empty() &&
        resolveJsonSkipCandidates(info.contextPlayerState.data(), info.contextPlayerState.size(), outIndex)) {
      ++spircContextPlayerSelect_;
      ++spircContextPlayerBySkipScan_;
      return true;
    }

    // Non-JSON schema-specific URI evidence remains supported. A naked context
    // index is intentionally not authoritative because modern indices may be
    // page-relative rather than indices into the retained classic field-27 queue.
    uint32_t schemaIndex = 0u;
    if (info.contextPlayerEncoding != F("json") && info.contextPlayerTargetUri.length() != 0u &&
        resolveRetainedIndexByUri(info.contextPlayerTargetUri, schemaIndex) && schemaIndex != trackRefIndex_) {
      outIndex = schemaIndex;
      ++spircContextPlayerSelect_;
      ++spircContextPlayerByUri_;
      if (info.contextPlayerUidResolved) ++spircContextPlayerByUid_;
      return true;
    }

    // r8a hardware proved that current Android wraps field 19 in gzip
    // (1f 8b 08...). Inflate the bounded payload using the ESP32-S3 ROM miniz
    // raw-DEFLATE helper. The output exists only for this frame and is never
    // serialized or retained. The gzip trailer's ISIZE is hard-bounded to
    // MAX_CONTEXT_PLAYER_INFLATED_BYTES and CRC32 is verified before parsing.
    if (!info.contextPlayerState.empty() && info.contextPlayerMagic == F("gzip")) {
      ++spircContextInflateAttempts_;
      ContextPlayerInflateResult inflated = inflateContextPlayerGzip(
          info.contextPlayerState.data(), info.contextPlayerState.size());
      strlcpy(spircContextInflateStatus_, inflated.status, sizeof(spircContextInflateStatus_));
      if (inflated.ok) {
        ++spircContextInflateOk_;
        spircContextInflateBytes_ = inflated.size;
        spircContextInflatePrintablePct_ = contextPlayerPrintablePct(inflated.data, inflated.size);

        String decodedEncoding;
        String decodedEndpoint;
        String decodedUid;
        String decodedUri;
        uint32_t decodedIndex = 0u;
        bool hasDecodedIndex = false;
        bool decodedUidResolved = false;
        parseContextPlayerState(inflated.data, inflated.size, decodedEncoding, decodedEndpoint,
                                decodedUid, decodedUri, decodedIndex, hasDecodedIndex,
                                decodedUidResolved);
        if (decodedEncoding.length() != 0u) {
          strlcpy(spircContextInflateEncoding_, decodedEncoding.c_str(), sizeof(spircContextInflateEncoding_));
        }
        if (decodedEndpoint.length() != 0u) {
          strlcpy(spircContextPlayerEndpoint_, decodedEndpoint.c_str(), sizeof(spircContextPlayerEndpoint_));
          if (decodedEndpoint == F("play")) ++spircContextPlayerPlay_;
        }

        bool selected = false;
        uint32_t decodedResolvedIndex = 0u;
        if (decodedEncoding == F("json") &&
            resolveJsonSkipCandidates(inflated.data, inflated.size, outIndex)) {
          ++spircContextPlayerSelect_;
          ++spircContextPlayerBySkipScan_;
          selected = true;
        } else if (decodedEncoding != F("json") && decodedUri.length() != 0u &&
                   resolveRetainedIndexByUri(decodedUri, decodedResolvedIndex) &&
                   decodedResolvedIndex != trackRefIndex_) {
          outIndex = decodedResolvedIndex;
          ++spircContextPlayerSelect_;
          ++spircContextPlayerByUri_;
          if (decodedUidResolved) ++spircContextPlayerByUid_;
          selected = true;
        } else if (scanRetainedQueue(inflated.data, inflated.size, outIndex)) {
          ++spircContextPlayerSelect_;
          ++spircContextPlayerByScan_;
          selected = true;
        } else if ((decodedEndpoint == F("play") || decodedEncoding == F("proto")) &&
                   (decodedUid.length() != 0u || decodedUri.length() != 0u || hasDecodedIndex)) {
          ++spircContextPlayerUnresolved_;
        }

        if (selected) ++spircContextPlayerByInflate_;
        freeContextPlayerInflate(inflated);
        if (selected) return true;
      } else {
        ++spircContextInflateFailures_;
      }
    }

    // Uncompressed/unknown fallback: correlate exact queue identities directly
    // against the bounded raw field-19 bytes. Act only on one unique non-current
    // match; never guess when the payload contains zero or multiple candidates.
    if (!info.contextPlayerState.empty() && scanRetainedQueue(
            info.contextPlayerState.data(), info.contextPlayerState.size(), outIndex)) {
      ++spircContextPlayerSelect_;
      ++spircContextPlayerByScan_;
      return true;
    }

    const bool selectionBearingPayload = info.contextPlayerEndpoint == F("play") ||
                                         info.contextPlayerEncoding == F("proto");
    if (selectionBearingPayload &&
        (info.contextPlayerTargetUid.length() != 0u || info.contextPlayerTargetUri.length() != 0u ||
         info.hasContextPlayerTargetIndex)) {
      ++spircContextPlayerUnresolved_;
    }
    return false;
  };

  auto sendSpircControlNotify = [&](SpircFrameInfo current, const char* positionSource) -> bool {
    const std::vector<uint8_t> frame = buildSpircTransferNotify(
        spircSequence_++, credentialDeviceId_, credentialDeviceName_,
        credentialVolume16_, syncedTimestampMs(), current, current.playStatus);
    spircControlNotifyBytes_ = frame.size();
    spircControlNotifyMercurySequence_ = mercurySequence_++;
    const std::vector<uint8_t> request = buildMercuryRequest(
        spircControlNotifyMercurySequence_, String(F("SEND")), subscriptionUri, {frame});
    if (request.empty() ||
        !sendShannonPacket(tcp, sendCipher, sendNonce, MERCURY_SEND_COMMAND, request, IO_TIMEOUT_MS)) {
      setError("SPIRC control Notify Mercury SEND failed");
      return false;
    }
    ++spircControlNotifySent_;
    ++txPackets_;
    if (current.hasLastCommandAck) ++spircCommandAcksSent_;
    spircLocalActive_ = true;
    spircLastLoadStatus_ = current.playStatus;
    spircLastLoadPositionMs_ = current.positionMs;
    setSpircPlaybackClock(current.playStatus, current.positionMs, positionSource);
    if (positionSource && strcmp(positionSource, "reset-track-change") == 0) ++spircPlaybackTrackResets_;
    return true;
  };

  auto clearAudioKeyCandidates = [&]() {
    memset(audioKeyCandidateFileIds_, 0, sizeof(audioKeyCandidateFileIds_));
    for (uint8_t i = 0u; i < MAX_AUDIO_KEY_CANDIDATES; ++i) audioKeyCandidateFormats_[i] = -1;
    memset(audioKeyCandidateResultCommand_, 0, sizeof(audioKeyCandidateResultCommand_));
    memset(audioKeyCandidateError0_, 0, sizeof(audioKeyCandidateError0_));
    memset(audioKeyCandidateError1_, 0, sizeof(audioKeyCandidateError1_));
    memset(audioKeyCandidateTimedOut_, 0, sizeof(audioKeyCandidateTimedOut_));
    audioKeyCandidateCount_ = 0u;
    audioKeyCandidateIndex_ = 0u;
    audioKeyCandidateAdvances_ = 0u;
    audioKeyCandidateTruncated_ = 0u;
  };

  auto sendTrackMetadataRequest = [&](const SpircFrameInfo& remote) -> bool {
    if (remote.selectedTrackGid.empty()) {
      setError("SPIRC Load selected track has no GID");
      return true; // activation remains valid; expose the metadata limitation diagnostically
    }
    const String gidHex = bytesToHex(remote.selectedTrackGid.data(), remote.selectedTrackGid.size());
    if (gidHex.length() != 32u) {
      setError("SPIRC Load selected track GID length unsupported");
      return true;
    }

    const bool haveSelectedTrack = trackRefGidHex_[0] != '\0';
    const bool sameTrack = haveSelectedTrack &&
        memcmp(selectedTrackGid_, remote.selectedTrackGid.data(), TRACK_GID_BYTES) == 0;

    // A duplicate Load for the same track can leave an in-flight metadata request alone.
    // A real track change supersedes both metadata and audio-key work from the old GID.
    if (metadataMercurySequence_ != ~static_cast<uint64_t>(0)) {
      if (sameTrack) return true;
      metadataMercurySequence_ = ~static_cast<uint64_t>(0);
      metadataRequestedAtMs_ = 0u;
    }
    if (sameTrack && (audioKeyPending_ || audioKeyCandidateCount_ != 0u)) {
      return true;
    }
    if (!sameTrack) {
      if (audioKeyPending_) {
        audioKeyPending_ = false;
        audioKeyPendingSequence_ = 0u;
        audioKeyRequestedAtMs_ = 0u;
        ++audioKeyTrackChangeCancels_;
      }
      clearAudioKeyCandidates();
      mediaHeadFetchedForTrack_ = false;
      mediaHeadHttpCode_ = 0;
      mediaHeadContentLength_ = -1;
      mediaHeadBytes_ = 0u;
      mediaHeadRangeHonored_ = false;
      mediaHeadOggCapture_ = false;
      setMediaHeadError("none");
      if (apStreamPending_) ++apStreamTrackChangeCancels_;
      apStreamChannelId_ = 0u;
      apStreamLastCompletedChannelId_ = 0xffffu;
      apStreamProbeIndex_ = 0u;
      apStreamCompletedProbes_ = 0u;
      apStreamRequestedAtMs_ = 0u;
      apStreamRequestBytes_ = 0u;
      apStreamResponsePackets_ = 0u;
      apStreamLastCommand_ = 0u;
      apStreamFailureCode_ = 0u;
      apStreamHeaderCount_ = 0u;
      apStreamHeaderBytes_ = 0u;
      apStreamReportedFileBytes_ = 0u;
      apStreamDataPackets_ = 0u;
      apStreamDataBytes_ = 0u;
      apStreamCurrentDataBytes_ = 0u;
      apStreamCandidateFormat_ = -1;
      apStreamHeadersComplete_ = false;
      apStreamPending_ = false;
      apStreamAttemptedForTrack_ = false;
      apStreamMediaSource_.reset();
      apStreamLiveVerifyAttempts_ = 0u;
      apStreamLiveVerifySuccesses_ = 0u;
      apStreamLiveVerifyFailures_ = 0u;
      apStreamLiveVerifyReadCalls_ = 0u;
      apStreamLiveVerifyChunks_ = 0u;
      apStreamLiveVerifyBytes_ = 0u;
      apStreamLiveVerifyHash_ = 2166136261u;
      apStreamLiveVerifyHashMatch_ = false;
      apStreamLiveVerifyEof_ = false;
      apStreamLiveVerifyRewound_ = false;
      setApStreamError("none");
    }

    trackRefIndex_ = remote.selectedTrackIndex;
    strlcpy(trackRefGidHex_, gidHex.c_str(), sizeof(trackRefGidHex_));
    const String effectiveTrackUri = remote.selectedTrackUri.length() != 0u
                                         ? remote.selectedTrackUri
                                         : spotifyTrackUriFromGid(remote.selectedTrackGid.data(),
                                                                  remote.selectedTrackGid.size());
    strlcpy(trackRefUri_, effectiveTrackUri.c_str(), sizeof(trackRefUri_));
    memcpy(selectedTrackGid_, remote.selectedTrackGid.data(), TRACK_GID_BYTES);
    memset(selectedAudioFileId_, 0, sizeof(selectedAudioFileId_));
    memset(audioKey_, 0, sizeof(audioKey_));
    audioKeyBytes_ = 0u;
    audioKeyLastCommand_ = 0u;
    audioKeyError0_ = 0u;
    audioKeyError1_ = 0u;
    clearMetadataAudit(spotify_metadata_audit::Status::Pending, selectedTrackGid_);
    const String metadataUri = String(TRACK_METADATA_PREFIX) + gidHex;
    metadataMercurySequence_ = mercurySequence_++;
    const std::vector<uint8_t> request = buildMercuryRequest(
        metadataMercurySequence_, String(F("GET")), metadataUri);
    ++metadataRequests_;
    if (request.empty() ||
        !sendShannonPacket(tcp, sendCipher, sendNonce, MERCURY_SEND_COMMAND, request, IO_TIMEOUT_MS)) {
      metadataMercurySequence_ = ~static_cast<uint64_t>(0);
      metadataRequestedAtMs_ = 0u;
      setError("track metadata Mercury GET failed");
      return false;
    }
    ++txPackets_;
    metadataRequestedAtMs_ = millis();
    return true;
  };

  auto sendAudioKeyCandidate = [&](uint8_t candidateIndex) -> bool {
    if (candidateIndex >= audioKeyCandidateCount_ || candidateIndex >= MAX_AUDIO_KEY_CANDIDATES) {
      ++audioKeyErrors_;
      ++audioKeyProtocolErrors_;
      setError("audio key candidate index invalid");
      return true;
    }
    if (audioKeyPending_) return true;

    audioKeyCandidateIndex_ = candidateIndex;
    memcpy(selectedAudioFileId_, audioKeyCandidateFileIds_[candidateIndex], AUDIO_FILE_ID_BYTES);
    memset(audioKey_, 0, sizeof(audioKey_));
    audioKeyBytes_ = 0u;
    audioKeyError0_ = 0u;
    audioKeyError1_ = 0u;
    const uint32_t sequence = audioKeyNextSequence_++;
    const std::vector<uint8_t> request = buildAudioKeyRequest(
        selectedAudioFileId_, selectedTrackGid_, sequence);
    audioKeyRequestBytes_ = request.size();
    audioKeyLastSequence_ = sequence;
    audioKeyPendingSequence_ = sequence;
    ++audioKeyRequests_;
    if (request.size() != AUDIO_FILE_ID_BYTES + TRACK_GID_BYTES + 6u ||
        !sendShannonPacket(tcp, sendCipher, sendNonce, REQUEST_KEY_COMMAND, request, IO_TIMEOUT_MS)) {
      audioKeyPending_ = false;
      audioKeyPendingSequence_ = 0u;
      ++audioKeyErrors_;
      ++audioKeyProtocolErrors_;
      setError("Spotify audio key RequestKey write failed");
      return false;
    }
    recordMetadataKeyTarget(selectedTrackGid_, selectedAudioFileId_, sequence, true);
    audioKeyPending_ = true;
    audioKeyRequestedAtMs_ = millis();
    ++txPackets_;
    return true;
  };

  auto advanceAudioKeyCandidate = [&]() -> bool {
    if (audioKeyCandidateCount_ == 0u ||
        static_cast<uint8_t>(audioKeyCandidateIndex_ + 1u) >= audioKeyCandidateCount_) {
      return true;
    }
    ++audioKeyCandidateAdvances_;
    return sendAudioKeyCandidate(static_cast<uint8_t>(audioKeyCandidateIndex_ + 1u));
  };

  auto sendApStreamProbe = [&](uint8_t probeIndex) -> bool {
    if (probeIndex >= AP_STREAM_PROBE_COUNT || audioKeyCandidateCount_ == 0u) return false;
    apStreamProbeIndex_ = probeIndex;
    apStreamChannelId_ = apStreamNextChannelId_++;
    apStreamRequestedAtMs_ = 0u;
    apStreamRequestBytes_ = 0u;
    apStreamLastCommand_ = 0u;
    apStreamFailureCode_ = 0u;
    apStreamCurrentDataBytes_ = 0u;
    apStreamHeadersComplete_ = false;
    apStreamPending_ = false;

    const uint32_t offsetWords = static_cast<uint32_t>(probeIndex) * AP_STREAM_CANARY_WORDS;
    const std::vector<uint8_t> request = buildApStreamChunkRequest(
        apStreamChannelId_, audioKeyCandidateFileIds_[0], offsetWords, AP_STREAM_CANARY_WORDS);
    apStreamRequestBytes_ = request.size();
    ++apStreamAttempts_;
    if (request.size() != 46u) {
      ++apStreamProtocolErrors_;
      setApStreamError("AP StreamChunk request construction failed");
      return true;
    }
    if (!sendShannonPacket(tcp, sendCipher, sendNonce, STREAM_CHUNK_REQUEST_COMMAND,
                           request, IO_TIMEOUT_MS)) {
      ++apStreamFailures_;
      setApStreamError("AP StreamChunk request write failed");
      return false;
    }
    ++txPackets_;
    apStreamPending_ = true;
    apStreamRequestedAtMs_ = millis();
    return true;
  };

  auto startApStreamCanary = [&]() -> bool {
    if (apStreamAttemptedForTrack_) return true;
    apStreamAttemptedForTrack_ = true;
    apStreamLastCompletedChannelId_ = 0xffffu;
    apStreamProbeIndex_ = 0u;
    apStreamCompletedProbes_ = 0u;
    apStreamResponsePackets_ = 0u;
    apStreamHeaderCount_ = 0u;
    apStreamHeaderBytes_ = 0u;
    apStreamReportedFileBytes_ = 0u;
    apStreamDataPackets_ = 0u;
    apStreamDataBytes_ = 0u;
    apStreamCurrentDataBytes_ = 0u;
    apStreamCipherHash_ = 2166136261u;
    apStreamCipherBytesHashed_ = 0u;
    apStreamSourceChunks_ = 0u;
    apStreamSourceChunkMismatches_ = 0u;
    apStreamMediaSource_.reset();
    apStreamLiveVerifyAttempts_ = 0u;
    apStreamLiveVerifySuccesses_ = 0u;
    apStreamLiveVerifyFailures_ = 0u;
    apStreamLiveVerifyReadCalls_ = 0u;
    apStreamLiveVerifyChunks_ = 0u;
    apStreamLiveVerifyBytes_ = 0u;
    apStreamLiveVerifyHash_ = 2166136261u;
    apStreamLiveVerifyHashMatch_ = false;
    apStreamLiveVerifyEof_ = false;
    apStreamLiveVerifyRewound_ = false;
    apStreamPostCompletePackets_ = 0u;
    setApStreamError("none");

    if (audioKeyCandidateCount_ == 0u) {
      ++apStreamProtocolErrors_;
      setApStreamError("no AudioFile candidate for AP stream");
      return true;
    }
    apStreamCandidateFormat_ = audioKeyCandidateFormats_[0];
    return sendApStreamProbe(0u);
  };

  // dev.2n-r13: exercise the live MediaChunkSource consumer contract without
  // opening any decrypt/decoder path. Read the sealed encrypted canary only
  // through next(), compare its bounded digest/shape with the independent AP
  // sourceGate, then rewind the read cursor while retaining the sealed bytes.
  auto verifyApStreamMediaSource = [&]() -> bool {
    ++apStreamLiveVerifyAttempts_;
    apStreamLiveVerifyReadCalls_ = 0u;
    apStreamLiveVerifyChunks_ = 0u;
    apStreamLiveVerifyBytes_ = 0u;
    apStreamLiveVerifyHash_ = 2166136261u;
    apStreamLiveVerifyHashMatch_ = false;
    apStreamLiveVerifyEof_ = false;
    apStreamLiveVerifyRewound_ = false;

    if (!apStreamMediaSource_.ready()) {
      ++apStreamLiveVerifyFailures_;
      return false;
    }

    apStreamMediaSource_.rewindRead();
    uint8_t scratch[512];
    bool readOk = true;
    while (!apStreamMediaSource_.eof()) {
      size_t written = 0u;
      if (!apStreamMediaSource_.next(scratch, sizeof(scratch), written) || written == 0u) {
        readOk = false;
        break;
      }
      ++apStreamLiveVerifyReadCalls_;
      if (apStreamLiveVerifyReadCalls_ > 64u) {
        readOk = false;
        break;
      }
      apStreamLiveVerifyHash_ = fnv1a32Update(apStreamLiveVerifyHash_, scratch, written);
      apStreamLiveVerifyBytes_ += written;
    }

    apStreamLiveVerifyChunks_ = apStreamMediaSource_.chunksSupplied();
    apStreamLiveVerifyEof_ = apStreamMediaSource_.eof();
    apStreamLiveVerifyHashMatch_ =
        apStreamLiveVerifyHash_ == apStreamCipherHash_ &&
        apStreamLiveVerifyBytes_ == apStreamCipherBytesHashed_ &&
        apStreamLiveVerifyChunks_ == apStreamSourceChunks_;

    apStreamMediaSource_.rewindRead();
    apStreamLiveVerifyRewound_ = apStreamMediaSource_.ready() &&
        apStreamMediaSource_.suppliedBytes() == 0u &&
        apStreamMediaSource_.chunksSupplied() == 0u;

    const bool ok = readOk && apStreamLiveVerifyEof_ && apStreamLiveVerifyHashMatch_ &&
        apStreamLiveVerifyRewound_ &&
        apStreamLiveVerifyBytes_ == AP_STREAM_CANARY_BYTES * AP_STREAM_PROBE_COUNT;
    if (ok) ++apStreamLiveVerifySuccesses_;
    else ++apStreamLiveVerifyFailures_;
    return ok;
  };

  while (!stopRequested_) {
    if (apContinuousAttemptedForTrack_ &&
        memcmp(apContinuousTrackGid_, selectedTrackGid_, sizeof(apContinuousTrackGid_)) != 0) {
      if (apContinuousPending_ || !apContinuousComplete_) ++apContinuousTrackChangeCancels_;
      clearApContinuousTrackState();
    }
    if (apExtendedAttemptedForTrack_ &&
        memcmp(apExtendedTrackGid_, selectedTrackGid_, sizeof(apExtendedTrackGid_)) != 0) {
      if (apExtendedPending_ || !apExtendedComplete_) {
        ++apExtendedTrackChangeCancels_;
        apExtendedLastCancelProducedBytes_ =
            static_cast<uint32_t>(apExtendedRing_.producedBytes());
        apExtendedLastCancelBufferedBytes_ =
            static_cast<uint32_t>(apExtendedRing_.bufferedBytes());
        strlcpy(apExtendedLastCancelStage_,
                apExtendedStage_ == 1u ? "sustained" :
                (apExtendedStage_ == 2u ? "tail" : "other"),
                sizeof(apExtendedLastCancelStage_));
        apExtendedRing_.invalidate();
      }
      clearApExtendedTrackState();
    }

    // dev.2n-r17: secondary continuous encrypted transport diagnostic. The
    // qualified 3x4096 canary above remains byte-for-byte frozen; only after it
    // has completed and its MediaChunkSource verification passed do we request
    // sixteen additional sequential 4096-byte ranges (64 KiB total). The bytes
    // are drained immediately through a bounded PSRAM-preferred ring by a hash-
    // only diagnostic consumer. No key, decryptor or decoder is connected here.
    auto drainApContinuousRing = [&]() -> bool {
      uint8_t scratch[512];
      while (apContinuousRing_.bufferedBytes() != 0u) {
        size_t written = 0u;
        if (!apContinuousRing_.pop(scratch, sizeof(scratch), written) || written == 0u) {
          return false;
        }
        apContinuousConsumerHash_ = fnv1a32Update(apContinuousConsumerHash_, scratch, written);
        apContinuousConsumerBytes_ += written;
        ++apContinuousConsumerReads_;
      }
      return true;
    };

    auto sendApContinuousRange = [&](uint8_t rangeIndex) -> bool {
      if (rangeIndex >= AP_CONTINUOUS_RANGE_COUNT || audioKeyCandidateCount_ == 0u) return false;
      apContinuousRangeIndex_ = rangeIndex;
      apContinuousChannelId_ = apStreamNextChannelId_++;
      apContinuousRequestedAtMs_ = 0u;
      apContinuousRequestBytes_ = 0u;
      apContinuousLastCommand_ = 0u;
      apContinuousFailureCode_ = 0u;
      apContinuousCurrentDataBytes_ = 0u;
      apContinuousHeadersComplete_ = false;
      apContinuousPending_ = false;

      const uint32_t offsetWords = static_cast<uint32_t>(rangeIndex) * AP_CONTINUOUS_RANGE_WORDS;
      const std::vector<uint8_t> request = buildApStreamChunkRequest(
          apContinuousChannelId_, audioKeyCandidateFileIds_[0], offsetWords,
          AP_CONTINUOUS_RANGE_WORDS);
      apContinuousRequestBytes_ = request.size();
      ++apContinuousAttempts_;
      if (request.size() != 46u) {
        ++apContinuousProtocolErrors_;
        setApContinuousError("AP continuous request construction failed");
        return true;
      }
      if (!sendShannonPacket(tcp, sendCipher, sendNonce, STREAM_CHUNK_REQUEST_COMMAND,
                             request, IO_TIMEOUT_MS)) {
        ++apContinuousFailures_;
        setApContinuousError("AP continuous request write failed");
        return false;
      }
      ++txPackets_;
      apContinuousPending_ = true;
      apContinuousRequestedAtMs_ = millis();
      return true;
    };

    auto startApContinuous = [&]() -> bool {
      if (apContinuousAttemptedForTrack_) return true;
      apContinuousAttemptedForTrack_ = true;
      apContinuousLastCompletedChannelId_ = 0xffffu;
      memcpy(apContinuousTrackGid_, selectedTrackGid_, sizeof(apContinuousTrackGid_));
      apContinuousRangeIndex_ = 0u;
      apContinuousCompletedRanges_ = 0u;
      apContinuousResponsePackets_ = 0u;
      apContinuousHeaderCount_ = 0u;
      apContinuousHeaderBytes_ = 0u;
      apContinuousReportedFileBytes_ = 0u;
      apContinuousDataPackets_ = 0u;
      apContinuousDataBytes_ = 0u;
      apContinuousCurrentDataBytes_ = 0u;
      apContinuousProducerHash_ = 2166136261u;
      apContinuousConsumerHash_ = 2166136261u;
      apContinuousConsumerBytes_ = 0u;
      apContinuousConsumerReads_ = 0u;
      apContinuousHashMatch_ = false;
      apContinuousEof_ = false;
      apContinuousComplete_ = false;
      setApContinuousError("none");

      if (audioKeyCandidateCount_ == 0u) {
        ++apContinuousProtocolErrors_;
        setApContinuousError("no AudioFile candidate for AP continuous transport");
        return true;
      }
      if (apStreamReportedFileBytes_ != 0u && apStreamReportedFileBytes_ < AP_CONTINUOUS_TARGET_BYTES) {
        ++apContinuousProtocolErrors_;
        setApContinuousError("audio file shorter than 64 KiB diagnostic target");
        return true;
      }
      if (!apContinuousRing_.begin(0u, AP_CONTINUOUS_TARGET_BYTES)) {
        ++apContinuousFailures_;
        setApContinuousError("AP continuous ring allocation failed");
        return true;
      }
      apContinuousCandidateFormat_ = audioKeyCandidateFormats_[0];
      return sendApContinuousRange(0u);
    };

    if (!apContinuousAttemptedForTrack_ && !apStreamPending_ &&
        apStreamSourceContractReady() && apStreamLiveVerifySuccesses_ != 0u) {
      if (!startApContinuous()) {
        tcp.stop(); state_ = State::Failed;
        setError("AP continuous transport start failed"); return false;
      }
    }

    // dev.2n-r19: after the frozen r17 64 KiB stage has passed, qualify a
    // separate 1 MiB rolling transfer (256 x 4096) through another bounded
    // 64 KiB ring, then request the exact final tail ending at reported EOF.
    // This remains hash-only encrypted-byte transport: no media key, decryptor,
    // Vorbis decoder or PCM consumer is connected.
    auto drainApExtendedRing = [&]() -> bool {
      uint8_t scratch[512];
      while (apExtendedRing_.bufferedBytes() != 0u) {
        size_t written = 0u;
        if (!apExtendedRing_.pop(scratch, sizeof(scratch), written) || written == 0u) return false;
        if (apExtendedStage_ == 1u) {
          apExtendedSustainedConsumerHash_ =
              fnv1a32Update(apExtendedSustainedConsumerHash_, scratch, written);
          apExtendedSustainedConsumerBytes_ += written;
          ++apExtendedSustainedConsumerReads_;
        } else if (apExtendedStage_ == 2u) {
          apExtendedTailConsumerHash_ =
              fnv1a32Update(apExtendedTailConsumerHash_, scratch, written);
          apExtendedTailConsumerBytes_ += written;
          ++apExtendedTailConsumerReads_;
        } else {
          return false;
        }
      }
      return true;
    };

    auto allocateApExtendedChannel = [&]() -> uint16_t {
      const uint16_t channel = apStreamNextChannelId_++;
      if (apExtendedAllocatedChannels_ == 0u) apExtendedFirstChannelId_ = channel;
      if (apExtendedAllocatedChannels_ != 0xffffu) ++apExtendedAllocatedChannels_;
      return channel;
    };

    auto sendApExtendedSustainedRange = [&](uint16_t rangeIndex) -> bool {
      if (rangeIndex >= AP_EXTENDED_SUSTAINED_RANGE_COUNT || audioKeyCandidateCount_ == 0u) return false;
      apExtendedStage_ = 1u;
      apExtendedSustainedRangeIndex_ = rangeIndex;
      apExtendedChannelId_ = allocateApExtendedChannel();
      apExtendedRequestedAtMs_ = 0u;
      apExtendedRequestBytes_ = 0u;
      apExtendedLastCommand_ = 0u;
      apExtendedFailureCode_ = 0u;
      apExtendedCurrentDataBytes_ = 0u;
      apExtendedHeadersComplete_ = false;
      apExtendedPending_ = false;

      const uint32_t absoluteOffset = AP_EXTENDED_SUSTAINED_START_BYTES +
          static_cast<uint32_t>(rangeIndex) * static_cast<uint32_t>(AP_EXTENDED_RANGE_BYTES);
      const std::vector<uint8_t> request = buildApStreamChunkRequest(
          apExtendedChannelId_, audioKeyCandidateFileIds_[0],
          absoluteOffset / AP_STREAM_WORD_BYTES, AP_EXTENDED_RANGE_WORDS);
      apExtendedRequestBytes_ = request.size();
      ++apExtendedAttempts_;
      if (request.size() != 46u) {
        ++apExtendedProtocolErrors_;
        apExtendedStage_ = 4u;
        setApExtendedError("AP extended sustained request construction failed");
        return true;
      }
      if (!sendShannonPacket(tcp, sendCipher, sendNonce, STREAM_CHUNK_REQUEST_COMMAND,
                             request, IO_TIMEOUT_MS)) {
        ++apExtendedFailures_;
        apExtendedStage_ = 4u;
        setApExtendedError("AP extended sustained request write failed");
        return false;
      }
      ++txPackets_;
      apExtendedPending_ = true;
      apExtendedRequestedAtMs_ = millis();
      return true;
    };

    auto sendApExtendedTail = [&]() -> bool {
      if (audioKeyCandidateCount_ == 0u || apExtendedTailTargetBytes_ == 0u ||
          (apExtendedTailTargetBytes_ % AP_STREAM_WORD_BYTES) != 0u) return false;
      apExtendedStage_ = 2u;
      apExtendedChannelId_ = allocateApExtendedChannel();
      apExtendedRequestedAtMs_ = 0u;
      apExtendedRequestBytes_ = 0u;
      apExtendedLastCommand_ = 0u;
      apExtendedFailureCode_ = 0u;
      apExtendedCurrentDataBytes_ = 0u;
      apExtendedHeadersComplete_ = false;
      apExtendedPending_ = false;
      const std::vector<uint8_t> request = buildApStreamChunkRequest(
          apExtendedChannelId_, audioKeyCandidateFileIds_[0],
          apExtendedTailStartBytes_ / AP_STREAM_WORD_BYTES,
          static_cast<uint32_t>(apExtendedTailTargetBytes_ / AP_STREAM_WORD_BYTES));
      apExtendedRequestBytes_ = request.size();
      ++apExtendedAttempts_;
      if (request.size() != 46u) {
        ++apExtendedProtocolErrors_;
        apExtendedStage_ = 4u;
        setApExtendedError("AP extended tail request construction failed");
        return true;
      }
      if (!sendShannonPacket(tcp, sendCipher, sendNonce, STREAM_CHUNK_REQUEST_COMMAND,
                             request, IO_TIMEOUT_MS)) {
        ++apExtendedFailures_;
        apExtendedStage_ = 4u;
        setApExtendedError("AP extended tail request write failed");
        return false;
      }
      ++txPackets_;
      apExtendedPending_ = true;
      apExtendedRequestedAtMs_ = millis();
      return true;
    };

    auto startApExtended = [&]() -> bool {
      if (apExtendedAttemptedForTrack_) return true;
      apExtendedAttemptedForTrack_ = true;
      memcpy(apExtendedTrackGid_, selectedTrackGid_, sizeof(apExtendedTrackGid_));
      apExtendedStage_ = 1u;
      apExtendedReportedFileBytes_ =
          apContinuousReportedFileBytes_ != 0u ? apContinuousReportedFileBytes_ : apStreamReportedFileBytes_;
      setApExtendedError("none");
      if (audioKeyCandidateCount_ == 0u) {
        ++apExtendedProtocolErrors_;
        apExtendedStage_ = 4u;
        setApExtendedError("no AudioFile candidate for AP extended transport");
        return true;
      }
      const uint32_t sustainedEnd = AP_EXTENDED_SUSTAINED_START_BYTES +
          static_cast<uint32_t>(AP_EXTENDED_SUSTAINED_TARGET_BYTES);
      if (apExtendedReportedFileBytes_ == 0u || apExtendedReportedFileBytes_ < sustainedEnd) {
        ++apExtendedProtocolErrors_;
        apExtendedStage_ = 4u;
        setApExtendedError("audio file too short or size unknown for 1 MiB diagnostic");
        return true;
      }
      if (!apExtendedRing_.begin(AP_EXTENDED_SUSTAINED_START_BYTES,
                                 AP_EXTENDED_SUSTAINED_TARGET_BYTES)) {
        ++apExtendedFailures_;
        apExtendedStage_ = 4u;
        setApExtendedError("AP extended ring allocation failed");
        return true;
      }
      return sendApExtendedSustainedRange(0u);
    };

    if (!apExtendedAttemptedForTrack_ && apContinuousComplete_ && !apContinuousPending_) {
      if (!startApExtended()) {
        tcp.stop(); state_ = State::Failed;
        setError("AP extended transport start failed"); return false;
      }
    }

    if (WiFi.status() != WL_CONNECTED) {
      tcp.stop(); state_ = State::Failed; setError("WiFi lost during Spotify session"); return false;
    }

    // r16: only this AP task writes diagnostics, using the same authenticated
    // Shannon channel and frozen RequestKey builder. Drain received commands
    // first; the diagnostic timeout still advances on busy receive iterations.
    spotify_key_probe::Request diagnostic{};
    if (takeKeyProbeRequest(diagnostic, tcp.available() <= 0)) {
      const std::vector<uint8_t> diagnosticWire = buildAudioKeyRequest(
          diagnostic.target.file, diagnostic.target.gid, diagnostic.sequence);
      const bool diagnosticWritten = diagnosticWire.size() == 42u &&
          sendShannonPacket(tcp, sendCipher, sendNonce, REQUEST_KEY_COMMAND,
                            diagnosticWire, IO_TIMEOUT_MS);
      finishKeyProbeWrite(diagnostic.sequence, diagnosticWritten);
      if (!diagnosticWritten) {
        tcp.stop(); state_ = State::Failed;
        setError("manual AudioKey diagnostic write failed"); return false;
      }
      ++txPackets_;
    }

    // dev.2n-r20: virtual EOS is a temporary player-lifecycle source while the
    // live decrypt/decoder consumer is intentionally closed. Evaluate only while
    // the AP socket is idle so a simultaneous remote PLAY/NEXT/LOAD frame wins the
    // race. The metadata generation that supplied duration must still be current;
    // this prevents stale duration from a prior track from advancing a new identity.
    if (tcp.available() <= 0 && metadataPlaybackGeneration_ == metadataAuditGeneration_) {
      const spotify_spirc_eos::Input eosInput{
          spircLocalActive_,
          spircPlaybackClockRunning_,
          spircLastLoadStatus_,
          currentSpircPositionMs(),
          metadataDurationMs_,
          static_cast<size_t>(trackRefIndex_),
          spircStateTrackRefs_.size(),
          spircStateHasRepeat_ && spircStateRepeat_,
          metadataPlaybackGeneration_,
          spircEosHandledGeneration_};
      const spotify_spirc_eos::Action eosAction = spotify_spirc_eos::decide(eosInput);
      if (eosAction != spotify_spirc_eos::Action::None) {
        spircEosHandledGeneration_ = metadataPlaybackGeneration_;
        ++spircVirtualEosEvents_;
        strlcpy(spircLastEosAction_, spotify_spirc_eos::actionName(eosAction),
                sizeof(spircLastEosAction_));

        if (eosAction == spotify_spirc_eos::Action::Advance) {
          ++spircAutoAdvanceAttempts_;
          const uint32_t nextIndex = trackRefIndex_ + 1u;
          SpircFrameInfo nextState = makeRetainedSpircState(nextIndex, 1u, 0u);
          if (!sendSpircControlNotify(nextState, "auto-next-eos")) {
            tcp.stop(); state_ = State::Failed; return false;
          }
          if (!sendTrackMetadataRequest(nextState)) {
            tcp.stop(); state_ = State::Failed; return false;
          }
          ++spircAutoAdvanceSuccesses_;
          // Re-enter the loop immediately: the selected GID has changed, so the
          // already-qualified r17/r19 per-track transports can cancel/wipe any old
          // in-flight state before the new metadata response is consumed.
          continue;
        }

        if (eosAction == spotify_spirc_eos::Action::HoldRepeat) {
          ++spircAutoAdvanceRepeatHolds_;
        } else if (eosAction == spotify_spirc_eos::Action::HoldBoundary) {
          ++spircAutoAdvanceBoundaryHolds_;
        }
      }
    }

    if (tcp.available() <= 0 && apExtendedPending_ && apExtendedRequestedAtMs_ != 0u &&
        millis() - apExtendedRequestedAtMs_ >= AP_EXTENDED_TIMEOUT_MS) {
      apExtendedPending_ = false;
      apExtendedRequestedAtMs_ = 0u;
      ++apExtendedTimeouts_;
      apExtendedRing_.invalidate();
      apExtendedStage_ = 4u;
      setApExtendedError("AP extended range response timeout");
    }

    if (tcp.available() <= 0 && apContinuousPending_ && apContinuousRequestedAtMs_ != 0u &&
        millis() - apContinuousRequestedAtMs_ >= AP_CONTINUOUS_TIMEOUT_MS) {
      apContinuousPending_ = false;
      apContinuousRequestedAtMs_ = 0u;
      ++apContinuousTimeouts_;
      apContinuousRing_.invalidate();
      setApContinuousError("AP continuous range response timeout");
    }

    if (tcp.available() <= 0) {
      if (audioKeyPending_ && audioKeyRequestedAtMs_ != 0u &&
          millis() - audioKeyRequestedAtMs_ >= AUDIO_KEY_TIMEOUT_MS) {
        const uint8_t timedOutCandidate = audioKeyCandidateIndex_;
        audioKeyPending_ = false;
        audioKeyPendingSequence_ = 0u;
        audioKeyRequestedAtMs_ = 0u;
        ++audioKeyTimeouts_;
        if (timedOutCandidate < audioKeyCandidateCount_) {
          audioKeyCandidateTimedOut_[timedOutCandidate] = true;
        }
        const bool hasNext = timedOutCandidate + 1u < audioKeyCandidateCount_;
        setError(hasNext ? "audio key candidate timeout; trying next" :
                           "audio key candidates exhausted after timeout");
        if (hasNext && !advanceAudioKeyCandidate()) {
          tcp.stop(); state_ = State::Failed; return false;
        }
        if (!hasNext && !startApStreamCanary()) {
          tcp.stop(); state_ = State::Failed; return false;
        }
      }
      if (apStreamPending_ && apStreamRequestedAtMs_ != 0u &&
          millis() - apStreamRequestedAtMs_ >= AP_STREAM_TIMEOUT_MS) {
        apStreamPending_ = false;
        apStreamRequestedAtMs_ = 0u;
        ++apStreamTimeouts_;
        apStreamMediaSource_.invalidate();
        setApStreamError("AP StreamChunk response timeout");
      }
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
    size_t declaredPayload = 0u;
    const char* readFailStage = "none";
    if (!recvShannonPacket(tcp, recvCipher, recvNonce, MAX_AP_ENCRYPTED_PACKET,
                           IO_TIMEOUT_MS, liveCommand, payload, liveMacOk,
                           &declaredPayload, &readFailStage)) {
      lastReadDeclaredPayload_ = declaredPayload;
      strlcpy(lastReadStage_, readFailStage ? readFailStage : "unknown", sizeof(lastReadStage_));
      if (readFailStage && strcmp(readFailStage, "oversize") == 0) ++oversizedPackets_;
      tcp.stop(); state_ = State::Failed; setError("Spotify session packet read failed"); return false;
    }
    lastReadDeclaredPayload_ = declaredPayload;
    strlcpy(lastReadStage_, "none", sizeof(lastReadStage_));
    ++rxPackets_;
    lastRxCommand_ = liveCommand;
    lastRxMs_ = millis();
    if (!liveMacOk) {
      ++shannonMacFailures_;
      tcp.stop(); state_ = State::Failed; setError("Shannon live MAC mismatch"); return false;
    }

    // Consume/quarantine diagnostic replies before the unchanged normal key
    // handler. Received key bytes are wiped here, including late responses.
    if (consumeKeyProbeResponse(liveCommand, payload)) continue;

    // r19 extended transport owns only the contiguous channel-id span allocated
    // after r17 completes. Packets outside that span fall through to the frozen
    // r17/canary handlers, preserving their late/stale accounting.
    if ((liveCommand == STREAM_CHUNK_SUCCESS_COMMAND ||
         liveCommand == STREAM_CHUNK_FAILURE_COMMAND) &&
        apExtendedAttemptedForTrack_) {
      bool extendedOwned = false;
      uint16_t extendedChannelId = 0u;
      if (payload.size() >= 2u) {
        extendedChannelId = readBe16At(payload, 0u);
        const uint16_t delta = static_cast<uint16_t>(extendedChannelId - apExtendedFirstChannelId_);
        extendedOwned = apExtendedAllocatedChannels_ != 0u && delta < apExtendedAllocatedChannels_;
      } else if (apExtendedPending_) {
        extendedOwned = true;
      }

      if (extendedOwned) {
        ++apExtendedResponsePackets_;
        apExtendedLastCommand_ = liveCommand;
        if (payload.size() < 2u) {
          ++apExtendedProtocolErrors_;
          apExtendedPending_ = false;
          apExtendedRequestedAtMs_ = 0u;
          apExtendedRing_.invalidate();
          apExtendedStage_ = 4u;
          setApExtendedError("AP extended response missing channel id");
          continue;
        }

        if (extendedChannelId == apExtendedLastCompletedChannelId_ &&
            (!apExtendedPending_ || extendedChannelId != apExtendedChannelId_)) {
          ++apExtendedPostCompletePackets_;
          continue;
        }
        if (extendedChannelId != apExtendedChannelId_ || !apExtendedPending_) {
          ++apExtendedStalePackets_;
          continue;
        }

        if (liveCommand == STREAM_CHUNK_FAILURE_COMMAND) {
          apExtendedPending_ = false;
          apExtendedRequestedAtMs_ = 0u;
          ++apExtendedFailures_;
          apExtendedRing_.invalidate();
          apExtendedStage_ = 4u;
          if (payload.size() >= 4u) {
            apExtendedFailureCode_ = readBe16At(payload, 2u);
            setApExtendedError("Spotify AP extended channel error");
          } else {
            ++apExtendedProtocolErrors_;
            setApExtendedError("AP extended error payload truncated");
          }
          continue;
        }

        size_t extendedOffset = 2u;
        const bool extendedWasDataState = apExtendedHeadersComplete_;
        bool extendedMalformed = false;
        if (!apExtendedHeadersComplete_) {
          while (extendedOffset < payload.size()) {
            if (payload.size() - extendedOffset < 2u) {
              extendedMalformed = true;
              break;
            }
            const uint16_t recordLength = readBe16At(payload, extendedOffset);
            extendedOffset += 2u;
            if (recordLength == 0u) {
              apExtendedHeadersComplete_ = true;
              break;
            }
            if (recordLength < 1u || recordLength > payload.size() - extendedOffset) {
              extendedMalformed = true;
              break;
            }
            const uint8_t headerId = payload[extendedOffset];
            const size_t headerDataBytes = static_cast<size_t>(recordLength - 1u);
            ++apExtendedHeaderCount_;
            apExtendedHeaderBytes_ += headerDataBytes;
            if (headerId == 0x03u && headerDataBytes == 4u) {
              const uint32_t words =
                  (static_cast<uint32_t>(payload[extendedOffset + 1u]) << 24u) |
                  (static_cast<uint32_t>(payload[extendedOffset + 2u]) << 16u) |
                  (static_cast<uint32_t>(payload[extendedOffset + 3u]) << 8u) |
                  static_cast<uint32_t>(payload[extendedOffset + 4u]);
              if (words <= 0x3fffffffu) {
                const uint32_t reported = words * AP_STREAM_WORD_BYTES;
                if (apExtendedReportedFileBytes_ != 0u &&
                    apExtendedReportedFileBytes_ != reported) {
                  extendedMalformed = true;
                  break;
                }
                apExtendedReportedFileBytes_ = reported;
              }
            }
            extendedOffset += static_cast<size_t>(recordLength);
          }
        }

        if (extendedMalformed) {
          apExtendedPending_ = false;
          apExtendedRequestedAtMs_ = 0u;
          ++apExtendedProtocolErrors_;
          apExtendedRing_.invalidate();
          apExtendedStage_ = 4u;
          setApExtendedError("AP extended header framing/size invalid");
          continue;
        }

        if (apExtendedHeadersComplete_) {
          if (extendedOffset < payload.size()) {
            const size_t packetDataBytes = payload.size() - extendedOffset;
            uint32_t absoluteOffset = 0u;
            if (apExtendedStage_ == 1u) {
              absoluteOffset = AP_EXTENDED_SUSTAINED_START_BYTES +
                  static_cast<uint32_t>(apExtendedSustainedRangeIndex_) *
                      static_cast<uint32_t>(AP_EXTENDED_RANGE_BYTES) +
                  static_cast<uint32_t>(apExtendedCurrentDataBytes_);
            } else if (apExtendedStage_ == 2u) {
              absoluteOffset = apExtendedTailStartBytes_ +
                  static_cast<uint32_t>(apExtendedCurrentDataBytes_);
            } else {
              apExtendedPending_ = false;
              apExtendedRequestedAtMs_ = 0u;
              ++apExtendedProtocolErrors_;
              apExtendedRing_.invalidate();
              apExtendedStage_ = 4u;
              setApExtendedError("AP extended data arrived outside active stage");
              continue;
            }

            if (!apExtendedRing_.push(absoluteOffset, payload.data() + extendedOffset,
                                      packetDataBytes)) {
              apExtendedPending_ = false;
              apExtendedRequestedAtMs_ = 0u;
              ++apExtendedProtocolErrors_;
              apExtendedRing_.invalidate();
              apExtendedStage_ = 4u;
              setApExtendedError("AP extended ring rejected producer bytes");
              continue;
            }
            apExtendedCurrentDataBytes_ += packetDataBytes;
            if (apExtendedStage_ == 1u) {
              apExtendedSustainedDataBytes_ += packetDataBytes;
              apExtendedSustainedProducerHash_ = fnv1a32Update(
                  apExtendedSustainedProducerHash_, payload.data() + extendedOffset, packetDataBytes);
            } else {
              apExtendedTailDataBytes_ += packetDataBytes;
              apExtendedTailProducerHash_ = fnv1a32Update(
                  apExtendedTailProducerHash_, payload.data() + extendedOffset, packetDataBytes);
            }
            if (!drainApExtendedRing()) {
              apExtendedPending_ = false;
              apExtendedRequestedAtMs_ = 0u;
              ++apExtendedProtocolErrors_;
              apExtendedRing_.invalidate();
              apExtendedStage_ = 4u;
              setApExtendedError("AP extended diagnostic consumer failed");
              continue;
            }
          } else if (extendedWasDataState) {
            apExtendedPending_ = false;
            apExtendedRequestedAtMs_ = 0u;
            apExtendedLastCompletedChannelId_ = apExtendedChannelId_;
            const size_t expectedRangeBytes = apExtendedStage_ == 1u ?
                AP_EXTENDED_RANGE_BYTES : apExtendedTailTargetBytes_;
            if (apExtendedCurrentDataBytes_ != expectedRangeBytes) {
              ++apExtendedProtocolErrors_;
              apExtendedRing_.invalidate();
              apExtendedStage_ = 4u;
              setApExtendedError(apExtendedCurrentDataBytes_ == 0u ?
                  "AP extended range closed without data" :
                  "AP extended range closed short/long");
              continue;
            }

            ++apExtendedSuccesses_;
            if (apExtendedStage_ == 1u) {
              ++apExtendedSustainedCompletedRanges_;
              if (apExtendedSustainedCompletedRanges_ == AP_EXTENDED_SUSTAINED_RANGE_COUNT) {
                if (!apExtendedRing_.finishProducer()) {
                  ++apExtendedProtocolErrors_;
                  apExtendedStage_ = 4u;
                  setApExtendedError("AP extended sustained producer finish mismatch");
                  continue;
                }
                apExtendedSustainedEof_ = apExtendedRing_.eof();
                apExtendedSustainedHashMatch_ =
                    apExtendedSustainedProducerHash_ == apExtendedSustainedConsumerHash_ &&
                    apExtendedSustainedDataBytes_ == AP_EXTENDED_SUSTAINED_TARGET_BYTES &&
                    apExtendedSustainedConsumerBytes_ == AP_EXTENDED_SUSTAINED_TARGET_BYTES &&
                    apExtendedRing_.producedBytes() == AP_EXTENDED_SUSTAINED_TARGET_BYTES &&
                    apExtendedRing_.consumedBytes() == AP_EXTENDED_SUSTAINED_TARGET_BYTES;
                snapshotApExtendedSustainedRing();
                const bool sustainedOk = apExtendedRing_.valid() && apExtendedSustainedEof_ &&
                    apExtendedSustainedHashMatch_ &&
                    apExtendedSustainedRingBackpressure_ == 0u &&
                    apExtendedSustainedRingGapErrors_ == 0u &&
                    apExtendedSustainedRingDuplicateErrors_ == 0u &&
                    apExtendedSustainedRingProducerErrors_ == 0u;
                if (!sustainedOk) {
                  ++apExtendedProtocolErrors_;
                  apExtendedStage_ = 4u;
                  setApExtendedError("AP extended sustained integrity verification failed");
                  continue;
                }

                apExtendedTailStartBytes_ =
                    ((apExtendedReportedFileBytes_ - 1u) / AP_EXTENDED_RANGE_BYTES) *
                    static_cast<uint32_t>(AP_EXTENDED_RANGE_BYTES);
                apExtendedTailTargetBytes_ =
                    static_cast<size_t>(apExtendedReportedFileBytes_ - apExtendedTailStartBytes_);
                if (apExtendedTailTargetBytes_ == 0u ||
                    apExtendedTailTargetBytes_ > AP_EXTENDED_RANGE_BYTES ||
                    (apExtendedTailTargetBytes_ % AP_STREAM_WORD_BYTES) != 0u) {
                  ++apExtendedProtocolErrors_;
                  apExtendedStage_ = 4u;
                  setApExtendedError("AP extended tail geometry invalid");
                  continue;
                }
                if (!apExtendedRing_.begin(apExtendedTailStartBytes_, apExtendedTailTargetBytes_)) {
                  ++apExtendedFailures_;
                  apExtendedStage_ = 4u;
                  setApExtendedError("AP extended tail ring reset failed");
                  continue;
                }
                if (!sendApExtendedTail()) {
                  tcp.stop(); state_ = State::Failed;
                  setError("AP extended tail write failed"); return false;
                }
              } else if (!sendApExtendedSustainedRange(apExtendedSustainedCompletedRanges_)) {
                tcp.stop(); state_ = State::Failed;
                setError("AP extended next-range write failed"); return false;
              }
            } else if (apExtendedStage_ == 2u) {
              if (!apExtendedRing_.finishProducer()) {
                ++apExtendedProtocolErrors_;
                apExtendedStage_ = 4u;
                setApExtendedError("AP extended tail producer finish mismatch");
                continue;
              }
              apExtendedTailEof_ = apExtendedRing_.eof();
              apExtendedTailHashMatch_ =
                  apExtendedTailProducerHash_ == apExtendedTailConsumerHash_ &&
                  apExtendedTailDataBytes_ == apExtendedTailTargetBytes_ &&
                  apExtendedTailConsumerBytes_ == apExtendedTailTargetBytes_ &&
                  apExtendedRing_.producedBytes() == apExtendedTailTargetBytes_ &&
                  apExtendedRing_.consumedBytes() == apExtendedTailTargetBytes_;
              apExtendedTailExactBoundary_ =
                  apExtendedTailStartBytes_ + apExtendedTailDataBytes_ ==
                  apExtendedReportedFileBytes_;
              apExtendedComplete_ = apExtendedRing_.valid() && apExtendedTailEof_ &&
                  apExtendedTailHashMatch_ && apExtendedTailExactBoundary_ &&
                  apExtendedRing_.backpressureEvents() == 0u &&
                  apExtendedRing_.gapErrors() == 0u &&
                  apExtendedRing_.duplicateErrors() == 0u &&
                  apExtendedRing_.producerErrors() == 0u &&
                  apExtendedSustainedHashMatch_ && apExtendedSustainedEof_;
              if (apExtendedComplete_) {
                apExtendedStage_ = 3u;
                setApExtendedError("none");
              } else {
                ++apExtendedProtocolErrors_;
                apExtendedStage_ = 4u;
                setApExtendedError("AP extended tail integrity verification failed");
              }
            }
          }
        }
        continue;
      }
    }

    // r17 continuous transport is a separate phase/channel namespace layered
    // after the qualified canary. Keeping this handler before the frozen canary
    // receiver preserves the r15/r16 AP StreamChunk regression block byte-for-byte.
    if ((liveCommand == STREAM_CHUNK_SUCCESS_COMMAND ||
         liveCommand == STREAM_CHUNK_FAILURE_COMMAND) &&
        apContinuousAttemptedForTrack_ && !apStreamPending_) {
      ++apContinuousResponsePackets_;
      apContinuousLastCommand_ = liveCommand;
      if (payload.size() < 2u) {
        ++apContinuousProtocolErrors_;
        if (apContinuousPending_) {
          apContinuousPending_ = false;
          apContinuousRequestedAtMs_ = 0u;
          apContinuousRing_.invalidate();
        }
        setApContinuousError("AP continuous response missing channel id");
        continue;
      }

      const uint16_t channelId = readBe16At(payload, 0u);
      if (channelId == apContinuousLastCompletedChannelId_ &&
          (!apContinuousPending_ || channelId != apContinuousChannelId_)) {
        ++apContinuousPostCompletePackets_;
        continue;
      }
      if (channelId != apContinuousChannelId_ || !apContinuousPending_) {
        ++apContinuousStalePackets_;
        continue;
      }

      if (liveCommand == STREAM_CHUNK_FAILURE_COMMAND) {
        apContinuousPending_ = false;
        apContinuousRequestedAtMs_ = 0u;
        ++apContinuousFailures_;
        apContinuousRing_.invalidate();
        if (payload.size() >= 4u) {
          apContinuousFailureCode_ = readBe16At(payload, 2u);
          setApContinuousError("Spotify AP continuous channel error");
        } else {
          ++apContinuousProtocolErrors_;
          setApContinuousError("AP continuous error payload truncated");
        }
        continue;
      }

      size_t continuousOffset = 2u;
      const bool continuousWasDataState = apContinuousHeadersComplete_;
      bool continuousMalformed = false;
      if (!apContinuousHeadersComplete_) {
        while (continuousOffset < payload.size()) {
          if (payload.size() - continuousOffset < 2u) {
            continuousMalformed = true;
            break;
          }
          const uint16_t recordLength = readBe16At(payload, continuousOffset);
          continuousOffset += 2u;
          if (recordLength == 0u) {
            apContinuousHeadersComplete_ = true;
            break;
          }
          if (recordLength < 1u || recordLength > payload.size() - continuousOffset) {
            continuousMalformed = true;
            break;
          }
          const uint8_t headerId = payload[continuousOffset];
          const size_t headerDataBytes = static_cast<size_t>(recordLength - 1u);
          ++apContinuousHeaderCount_;
          apContinuousHeaderBytes_ += headerDataBytes;
          if (headerId == 0x03u && headerDataBytes == 4u) {
            const uint32_t words =
                (static_cast<uint32_t>(payload[continuousOffset + 1u]) << 24u) |
                (static_cast<uint32_t>(payload[continuousOffset + 2u]) << 16u) |
                (static_cast<uint32_t>(payload[continuousOffset + 3u]) << 8u) |
                static_cast<uint32_t>(payload[continuousOffset + 4u]);
            if (words <= 0x3fffffffu) {
              apContinuousReportedFileBytes_ = words * AP_STREAM_WORD_BYTES;
            }
          }
          continuousOffset += static_cast<size_t>(recordLength);
        }
      }

      if (continuousMalformed) {
        apContinuousPending_ = false;
        apContinuousRequestedAtMs_ = 0u;
        ++apContinuousProtocolErrors_;
        apContinuousRing_.invalidate();
        setApContinuousError("AP continuous header framing invalid");
        continue;
      }

      if (apContinuousHeadersComplete_) {
        if (continuousOffset < payload.size()) {
          const size_t packetDataBytes = payload.size() - continuousOffset;
          const uint32_t absoluteOffset =
              static_cast<uint32_t>(apContinuousRangeIndex_) * AP_CONTINUOUS_RANGE_BYTES +
              static_cast<uint32_t>(apContinuousCurrentDataBytes_);
          if (!apContinuousRing_.push(absoluteOffset, payload.data() + continuousOffset,
                                      packetDataBytes)) {
            apContinuousPending_ = false;
            apContinuousRequestedAtMs_ = 0u;
            ++apContinuousProtocolErrors_;
            apContinuousRing_.invalidate();
            setApContinuousError("AP continuous ring rejected producer bytes");
            continue;
          }
          ++apContinuousDataPackets_;
          apContinuousDataBytes_ += packetDataBytes;
          apContinuousCurrentDataBytes_ += packetDataBytes;
          apContinuousProducerHash_ = fnv1a32Update(
              apContinuousProducerHash_, payload.data() + continuousOffset, packetDataBytes);
          if (!drainApContinuousRing()) {
            apContinuousPending_ = false;
            apContinuousRequestedAtMs_ = 0u;
            ++apContinuousProtocolErrors_;
            apContinuousRing_.invalidate();
            setApContinuousError("AP continuous diagnostic consumer failed");
            continue;
          }
        } else if (continuousWasDataState) {
          apContinuousPending_ = false;
          apContinuousRequestedAtMs_ = 0u;
          apContinuousLastCompletedChannelId_ = apContinuousChannelId_;
          if (apContinuousCurrentDataBytes_ == AP_CONTINUOUS_RANGE_BYTES) {
            ++apContinuousSuccesses_;
            ++apContinuousCompletedRanges_;
            if (apContinuousCompletedRanges_ == AP_CONTINUOUS_RANGE_COUNT) {
              if (!apContinuousRing_.finishProducer()) {
                ++apContinuousProtocolErrors_;
                setApContinuousError("AP continuous producer finish mismatch");
                continue;
              }
              apContinuousEof_ = apContinuousRing_.eof();
              apContinuousHashMatch_ =
                  apContinuousProducerHash_ == apContinuousConsumerHash_ &&
                  apContinuousConsumerBytes_ == AP_CONTINUOUS_TARGET_BYTES &&
                  apContinuousRing_.producedBytes() == AP_CONTINUOUS_TARGET_BYTES &&
                  apContinuousRing_.consumedBytes() == AP_CONTINUOUS_TARGET_BYTES;
              apContinuousComplete_ = apContinuousRing_.valid() && apContinuousEof_ &&
                  apContinuousHashMatch_ && apContinuousRing_.gapErrors() == 0u &&
                  apContinuousRing_.duplicateErrors() == 0u &&
                  apContinuousRing_.producerErrors() == 0u;
              if (apContinuousComplete_) {
                setApContinuousError("none");
              } else {
                ++apContinuousProtocolErrors_;
                setApContinuousError("AP continuous integrity verification failed");
              }
            } else if (!sendApContinuousRange(apContinuousCompletedRanges_)) {
              tcp.stop(); state_ = State::Failed;
              setError("AP continuous next-range write failed"); return false;
            }
          } else if (apContinuousCurrentDataBytes_ != 0u) {
            ++apContinuousProtocolErrors_;
            apContinuousRing_.invalidate();
            setApContinuousError("AP continuous range closed short/long");
          } else {
            ++apContinuousProtocolErrors_;
            apContinuousRing_.invalidate();
            setApContinuousError("AP continuous range closed without data");
          }
        }
      }
      continue;
    }

    if (liveCommand == STREAM_CHUNK_SUCCESS_COMMAND ||
        liveCommand == STREAM_CHUNK_FAILURE_COMMAND) {
      ++apStreamResponsePackets_;
      apStreamLastCommand_ = liveCommand;
      if (payload.size() < 2u) {
        ++apStreamProtocolErrors_;
        if (apStreamPending_) {
          apStreamPending_ = false;
          apStreamRequestedAtMs_ = 0u;
        }
        setApStreamError("AP stream response missing channel id");
        continue;
      }
      const uint16_t channelId = readBe16At(payload, 0u);
      if (apStreamAttemptedForTrack_ && channelId == apStreamLastCompletedChannelId_ &&
          (!apStreamPending_ || channelId != apStreamChannelId_)) {
        ++apStreamPostCompletePackets_;
        continue;
      }
      if (!apStreamAttemptedForTrack_ || channelId != apStreamChannelId_ || !apStreamPending_) {
        ++apStreamStalePackets_;
        continue;
      }

      if (liveCommand == STREAM_CHUNK_FAILURE_COMMAND) {
        apStreamPending_ = false;
        apStreamRequestedAtMs_ = 0u;
        ++apStreamFailures_;
        apStreamMediaSource_.invalidate();
        if (payload.size() >= 4u) {
          apStreamFailureCode_ = readBe16At(payload, 2u);
          setApStreamError("Spotify AP StreamChunk channel error");
        } else {
          ++apStreamProtocolErrors_;
          setApStreamError("AP StreamChunk error payload truncated");
        }
        continue;
      }

      size_t offset = 2u;
      const bool wasDataState = apStreamHeadersComplete_;
      bool packetMalformed = false;
      if (!apStreamHeadersComplete_) {
        while (offset < payload.size()) {
          if (payload.size() - offset < 2u) {
            packetMalformed = true;
            break;
          }
          const uint16_t recordLength = readBe16At(payload, offset);
          offset += 2u;
          if (recordLength == 0u) {
            apStreamHeadersComplete_ = true;
            break;
          }
          if (recordLength < 1u || recordLength > payload.size() - offset) {
            packetMalformed = true;
            break;
          }
          const uint8_t headerId = payload[offset];
          const size_t headerDataBytes = static_cast<size_t>(recordLength - 1u);
          ++apStreamHeaderCount_;
          apStreamHeaderBytes_ += headerDataBytes;
          if (headerId == 0x03u && headerDataBytes == 4u) {
            const uint32_t words = (static_cast<uint32_t>(payload[offset + 1u]) << 24u) |
                                   (static_cast<uint32_t>(payload[offset + 2u]) << 16u) |
                                   (static_cast<uint32_t>(payload[offset + 3u]) << 8u) |
                                   static_cast<uint32_t>(payload[offset + 4u]);
            if (words <= 0x3fffffffu) apStreamReportedFileBytes_ = words * AP_STREAM_WORD_BYTES;
          }
          offset += static_cast<size_t>(recordLength);
        }
      }

      if (packetMalformed) {
        apStreamPending_ = false;
        apStreamRequestedAtMs_ = 0u;
        ++apStreamProtocolErrors_;
        apStreamMediaSource_.invalidate();
        setApStreamError("AP StreamChunk header framing invalid");
        continue;
      }

      if (apStreamHeadersComplete_) {
        if (offset < payload.size()) {
          const size_t packetDataBytes = payload.size() - offset;
          ++apStreamDataPackets_;
          apStreamDataBytes_ += packetDataBytes;
          apStreamCurrentDataBytes_ += packetDataBytes;
          apStreamCipherHash_ = fnv1a32Update(apStreamCipherHash_, payload.data() + offset, packetDataBytes);
          apStreamCipherBytesHashed_ += packetDataBytes;
          // dev.2n-r13: retain only the bounded encrypted 3x4096-byte canary
          // in transient memory behind the same MediaChunkSource contract used
          // by local fixtures. No live key/decrypt/decoder consumer is wired.
          apStreamMediaSource_.appendFragment(payload.data() + offset, packetDataBytes);
        } else if (wasDataState) {
          apStreamPending_ = false;
          apStreamRequestedAtMs_ = 0u;
          apStreamLastCompletedChannelId_ = apStreamChannelId_;
          if (apStreamCurrentDataBytes_ >= AP_STREAM_CANARY_BYTES) {
            ++apStreamSuccesses_;
            ++apStreamCompletedProbes_;
            if (apStreamCurrentDataBytes_ == AP_STREAM_CANARY_BYTES) {
              ++apStreamSourceChunks_;
              if (!apStreamMediaSource_.finishChunk()) {
                ++apStreamLiveVerifyFailures_;
              } else if (apStreamCompletedProbes_ == AP_STREAM_PROBE_COUNT &&
                         apStreamMediaSource_.ready()) {
                verifyApStreamMediaSource();
              }
            } else {
              ++apStreamSourceChunkMismatches_;
              apStreamMediaSource_.invalidate();
            }
            setApStreamError("none");
            if (apStreamCompletedProbes_ < AP_STREAM_PROBE_COUNT) {
              if (!sendApStreamProbe(apStreamCompletedProbes_)) {
                tcp.stop(); state_ = State::Failed; return false;
              }
            }
          } else if (apStreamCurrentDataBytes_ != 0u) {
            ++apStreamProtocolErrors_;
            apStreamMediaSource_.invalidate();
            setApStreamError("AP StreamChunk closed short");
          } else {
            ++apStreamProtocolErrors_;
            apStreamMediaSource_.invalidate();
            setApStreamError("AP StreamChunk closed without data");
          }
        }
      }
      continue;
    }

    if (liveCommand == AES_KEY_COMMAND || liveCommand == AES_KEY_ERROR_COMMAND) {
      uint32_t responseSequence = 0u;
      ++audioKeyResponses_;
      if (!readBe32Prefix(payload, responseSequence)) {
        ++audioKeyErrors_;
        ++audioKeyProtocolErrors_;
        audioKeyPending_ = false;
        audioKeyPendingSequence_ = 0u;
        audioKeyRequestedAtMs_ = 0u;
        setError("audio key response missing sequence");
        continue;
      }
      if (!audioKeyPending_ || responseSequence != audioKeyPendingSequence_) {
        ++audioKeyStaleResponses_;
        continue;
      }

      const uint8_t candidateIndex = audioKeyCandidateIndex_;
      audioKeyLastCommand_ = liveCommand;
      audioKeyLastSequence_ = responseSequence;
      audioKeyPending_ = false;
      audioKeyPendingSequence_ = 0u;
      audioKeyRequestedAtMs_ = 0u;
      if (candidateIndex < audioKeyCandidateCount_) {
        audioKeyCandidateResultCommand_[candidateIndex] = liveCommand;
      }

      if (liveCommand == AES_KEY_COMMAND) {
        if (payload.size() != 4u + AUDIO_AES_KEY_BYTES) {
          ++audioKeyErrors_;
          ++audioKeyProtocolErrors_;
          audioKeyBytes_ = 0u;
          memset(audioKey_, 0, sizeof(audioKey_));
          setError("audio key response length invalid");
          continue;
        }
        memcpy(audioKey_, payload.data() + 4u, AUDIO_AES_KEY_BYTES);
        audioKeyBytes_ = AUDIO_AES_KEY_BYTES;
        ++audioKeySuccesses_;
        setError("none");
        if (!startApStreamCanary()) {
          tcp.stop(); state_ = State::Failed; return false;
        }
      } else {
        ++audioKeyErrors_;
        ++audioKeyServiceRejects_;
        audioKeyBytes_ = 0u;
        memset(audioKey_, 0, sizeof(audioKey_));
        if (payload.size() < 6u) {
          ++audioKeyProtocolErrors_;
          setError("audio key error response length invalid");
          continue;
        }
        audioKeyError0_ = payload[4];
        audioKeyError1_ = payload[5];
        if (candidateIndex < audioKeyCandidateCount_) {
          audioKeyCandidateError0_[candidateIndex] = audioKeyError0_;
          audioKeyCandidateError1_[candidateIndex] = audioKeyError1_;
        }
        const bool hasNext = candidateIndex + 1u < audioKeyCandidateCount_;
        if (hasNext) {
          setError("Spotify AesKeyError; trying next audio file");
        } else {
          bool allRejected01 = audioKeyCandidateCount_ != 0u;
          for (uint8_t i = 0u; i < audioKeyCandidateCount_; ++i) {
            if (audioKeyCandidateResultCommand_[i] != AES_KEY_ERROR_COMMAND ||
                audioKeyCandidateError0_[i] != 0u || audioKeyCandidateError1_[i] != 1u) {
              allRejected01 = false;
              break;
            }
          }
          if (allRejected01) {
            if (!mediaKeyServiceBlocked_) ++mediaKeyBlockEvents_;
            mediaKeyServiceBlocked_ = true;
            mediaKeyBlockError0_ = 0u;
            mediaKeyBlockError1_ = 1u;
            setError("Spotify media key service-blocked (0:1)");
            if (!sendSpircBlockedNotify()) {
              tcp.stop(); state_ = State::Failed; return false;
            }
          } else {
            setError("Spotify AesKeyError for all audio files");
          }
        }
        if (hasNext && !advanceAudioKeyCandidate()) {
          tcp.stop(); state_ = State::Failed; return false;
        }
        if (!hasNext && !startApStreamCanary()) {
          tcp.stop(); state_ = State::Failed; return false;
        }
      }
      state_ = State::SpircReady;
      continue;
    }

    if (liveCommand == PRODUCT_INFO_COMMAND) {
      ++productInfoPackets_;
      productInfoBytes_ = payload.size();
      productInfoHash_ = 2166136261u;  // FNV-1a, diagnostic identity only.
      for (const uint8_t byte : payload) {
        productInfoHash_ ^= byte;
        productInfoHash_ *= 16777619u;
      }
      productInfoXmlLike_ = false;
      for (size_t i = 0u; i < payload.size() && i < 16u; ++i) {
        const uint8_t byte = payload[i];
        if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') continue;
        productInfoXmlLike_ = byte == '<';
        break;
      }

      auto copyProductField = [&payload](const char* tag, char* dst, size_t dstSize) {
        String field;
        if (SpotifySessionProbe::extractXmlTag(payload, tag, field) && field.length() < dstSize) {
          strlcpy(dst, field.length() ? field.c_str() : "empty", dstSize);
        } else {
          strlcpy(dst, "missing", dstSize);
        }
      };
      copyProductField("type", productInfoType_, sizeof(productInfoType_));
      copyProductField("catalogue", productInfoCatalogue_, sizeof(productInfoCatalogue_));
      copyProductField("player-license", productInfoPlayerLicense_, sizeof(productInfoPlayerLicense_));
      copyProductField("head-files", productInfoHeadFiles_, sizeof(productInfoHeadFiles_));
      copyProductField("on-demand", productInfoOnDemand_, sizeof(productInfoOnDemand_));
      copyProductField("high-bitrate", productInfoHighBitrate_, sizeof(productInfoHighBitrate_));
      copyProductField("unrestricted", productInfoUnrestricted_, sizeof(productInfoUnrestricted_));
      copyProductField("mobile", productInfoMobile_, sizeof(productInfoMobile_));
      copyProductField("prefetch-keys", productInfoPrefetchKeys_, sizeof(productInfoPrefetchKeys_));
      copyProductField("key-memory-cache-mode", productInfoKeyMemoryCacheMode_, sizeof(productInfoKeyMemoryCacheMode_));
      copyProductField("key-caching-max-count", productInfoKeyCachingMaxCount_, sizeof(productInfoKeyCachingMaxCount_));

      String headTemplate;
      if (extractXmlTag(payload, "head-files-url", headTemplate) && headTemplate.length() != 0u) {
        if (headTemplate.length() < sizeof(headFileTemplate_)) {
          strlcpy(headFileTemplate_, headTemplate.c_str(), sizeof(headFileTemplate_));
          if (headTemplate.startsWith(F("http://"))) {
            strlcpy(headFileScheme_, "http", sizeof(headFileScheme_));
          } else if (headTemplate.startsWith(F("https://"))) {
            strlcpy(headFileScheme_, "https", sizeof(headFileScheme_));
          } else {
            strlcpy(headFileScheme_, "other", sizeof(headFileScheme_));
          }
        } else {
          memset(headFileTemplate_, 0, sizeof(headFileTemplate_));
          strlcpy(headFileScheme_, "oversize", sizeof(headFileScheme_));
          setMediaHeadError("head-files-url attribute too long");
        }
      } else {
        memset(headFileTemplate_, 0, sizeof(headFileTemplate_));
        strlcpy(headFileScheme_, "none", sizeof(headFileScheme_));
      }
      if (!mediaHeadFetchedForTrack_ && audioKeyCandidateCount_ != 0u && !audioKeyPending_) {
        const uint8_t current = audioKeyCandidateIndex_;
        const bool terminalSuccess = audioKeyBytes_ == AUDIO_AES_KEY_BYTES;
        const bool terminalReject = current + 1u >= audioKeyCandidateCount_ &&
            audioKeyCandidateResultCommand_[current] == AES_KEY_ERROR_COMMAND;
        const bool terminalTimeout = current + 1u >= audioKeyCandidateCount_ &&
            audioKeyCandidateTimedOut_[current];
        if ((terminalSuccess || terminalReject || terminalTimeout) && !startApStreamCanary()) {
          tcp.stop(); state_ = State::Failed; return false;
        }
      }
      continue;
    }

    if (liveCommand == PING_COMMAND) {
      ++pingReceived_;
      if (payload.size() >= 4u) {
        serverTimestampSeconds_ = (static_cast<uint32_t>(payload[0]) << 24u) |
                                  (static_cast<uint32_t>(payload[1]) << 16u) |
                                  (static_cast<uint32_t>(payload[2]) << 8u) |
                                  static_cast<uint32_t>(payload[3]);
        serverTimestampLocalMs_ = millis();
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

      if (subscriptionReadyThisSession) {
        state_ = State::SessionActive;
        if (!sendSpircHello()) { tcp.stop(); state_ = State::Failed; return false; }
      }
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
      std::vector<std::vector<uint8_t>> parts;
      int32_t mercuryStatus = 0;
      if (parseMercuryEnvelope(payload, sequence, uri, method, parts, &mercuryStatus)) {
        mercuryLastSequence_ = sequence;
        if (uri.length() != 0u) strlcpy(mercuryLastUri_, uri.c_str(), sizeof(mercuryLastUri_));

        bool subscriptionBecameReady = false;
        if (liveCommand == MERCURY_EVENT_COMMAND) {
          ++mercuryEvents_;
          if (subscriptionSent && isSpircSubscriptionUri(uri)) {
            if (uri == subscriptionUri) ++spircUriRootEvents_;
            else ++spircUriChildEvents_;
            ++mercurySubResponses_;
            subscriptionReadyThisSession = true;
            state_ = State::SessionActive;
            subscriptionBecameReady = true;

            for (const auto& part : parts) {
              SpircFrameInfo info;
              if (!parseSpircFrame(part, info)) continue;
              ++spircRxFrames_;
              spircLastType_ = info.type;
              const bool self = info.ident.length() != 0u && info.ident == String(credentialDeviceId_);
              if (self) {
                ++spircSelfEchoes_;
              } else {
                ++spircRemoteFrames_;
                if (info.ident.length() != 0u) strlcpy(spircRemoteIdent_, info.ident.c_str(), sizeof(spircRemoteIdent_));
                if (info.name.length() != 0u) strlcpy(spircRemoteName_, info.name.c_str(), sizeof(spircRemoteName_));
                if (info.hasActive) spircRemoteActive_ = info.active;
              }
              bool addressedToUs = info.recipients.empty();
              for (const String& recipient : info.recipients) {
                if (recipient == String(credentialDeviceId_)) { addressedToUs = true; break; }
              }
              if (!self && !addressedToUs) {
                ++spircRecipientIgnored_;
                state_ = State::SpircReady;
                continue;
              }
              if (!self) rememberSpircCommandAck(info);

              if (info.type == SPIRC_NOTIFY) {
                ++spircNotifyFrames_;
              } else if (info.type == SPIRC_LOAD) {
                ++spircLoadFrames_;
                const uint32_t selectionApplyStartedUs = micros();
                uint32_t contextSelectedIndex = trackRefIndex_;
                if (!self && resolveContextPlayerSelection(info, contextSelectedIndex) &&
                    contextSelectedIndex < spircStateTrackRefs_.size() && contextSelectedIndex != trackRefIndex_) {
                  // r18: the context-player frame can carry the previous track's
                  // running position. Once the resolved queue identity changes, that
                  // position is not authoritative for the new media item.
                  SpircFrameInfo selected = makeRetainedSpircState(contextSelectedIndex,
                      info.hasPlayStatus && info.playStatus == 2u ? 2u : 1u, 0u);
                  if (!sendSpircControlNotify(selected, "reset-track-change")) { tcp.stop(); state_ = State::Failed; return false; }
                  spircSelectionApplyLastUs_ = micros() - selectionApplyStartedUs;
                  if (spircSelectionApplyLastUs_ > spircSelectionApplyMaxUs_) {
                    spircSelectionApplyMaxUs_ = spircSelectionApplyLastUs_;
                  }
                  if (!sendTrackMetadataRequest(selected)) { tcp.stop(); state_ = State::Failed; return false; }
                  state_ = State::SpircReady;
                  continue;
                }
                const bool hasTrackIdentity =
                    info.selectedTrackGid.size() == TRACK_GID_BYTES ||
                    info.selectedTrackUri.length() != 0u || info.trackCount != 0u;
                if (!self && !hasTrackIdentity) {
                  ++spircEmptyLoadsIgnored_;
                  SpircFrameInfo current = makeRetainedSpircState(trackRefIndex_,
                      spircLastLoadStatus_ == 1u ? 1u : 2u, currentSpircPositionMs());
                  if (!sendSpircControlNotify(current, "retain-empty-load")) { tcp.stop(); state_ = State::Failed; return false; }
                  state_ = State::SpircReady;
                  continue;
                }
                const bool duplicateTrack = !self && spircLocalActive_ &&
                    info.selectedTrackGid.size() == TRACK_GID_BYTES &&
                    trackRefGidHex_[0] != '\0' &&
                    memcmp(selectedTrackGid_, info.selectedTrackGid.data(), TRACK_GID_BYTES) == 0;
                if (duplicateTrack) {
                  ++spircDuplicateLoadsIgnored_;
                  if (info.hasContextPlayerState) ++spircDuplicateLoadsWithUnknownContext_;
                  SpircFrameInfo current = makeRetainedSpircState(trackRefIndex_,
                      spircLastLoadStatus_ == 1u ? 1u : 2u, currentSpircPositionMs());
                  if (!sendSpircControlNotify(current, "retain-duplicate-load")) { tcp.stop(); state_ = State::Failed; return false; }
                  ++spircDuplicateLoadsAcked_;
                  state_ = State::SpircReady;
                  continue;
                }
                if (!self) {
                  const bool directTrackChange = spircLocalActive_ &&
                      info.selectedTrackGid.size() == TRACK_GID_BYTES && trackRefGidHex_[0] != '\0' &&
                      memcmp(selectedTrackGid_, info.selectedTrackGid.data(), TRACK_GID_BYTES) != 0;
                  SpircFrameInfo loadState = info;
                  if (directTrackChange) {
                    loadState.position = 0u;
                    loadState.positionMs = 0u;
                    loadState.hasPosition = false;
                    loadState.hasPositionMs = true;
                  }
                  if (!sendSpircTransferNotify(loadState,
                          directTrackChange ? "reset-track-change" : "remote-load")) {
                    tcp.stop(); state_ = State::Failed; return false;
                  }
                  if (!sendTrackMetadataRequest(loadState)) {
                    tcp.stop(); state_ = State::Failed; return false;
                  }
                }
              } else if (info.type == SPIRC_REPLACE) {
                ++spircReplaceFrames_;
                uint32_t contextSelectedIndex = trackRefIndex_;
                const bool selected = !self && resolveContextPlayerSelection(info, contextSelectedIndex);
                if (!self && selected && contextSelectedIndex < spircStateTrackRefs_.size()) {
                  SpircFrameInfo replacement = makeRetainedSpircState(contextSelectedIndex, 1u, 0u);
                  if (!sendSpircControlNotify(replacement, "reset-track-change")) { tcp.stop(); state_ = State::Failed; return false; }
                  if (contextSelectedIndex != trackRefIndex_ && !sendTrackMetadataRequest(replacement)) {
                    tcp.stop(); state_ = State::Failed; return false;
                  }
                } else if (!self) {
                  // Replace is also used by classic controllers for operations such as
                  // add_to_queue. Keep those bounded/non-destructive until implemented,
                  // but ACK our current state so the controller does not retry forever.
                  SpircFrameInfo current = makeRetainedSpircState(trackRefIndex_,
                      spircLastLoadStatus_ == 1u ? 1u : 2u, currentSpircPositionMs());
                  if (!sendSpircControlNotify(current, "retain-replace")) { tcp.stop(); state_ = State::Failed; return false; }
                }
              } else if (info.type == SPIRC_PLAY) {
                ++spircPlayFrames_;
                if (!self) {
                  // A current controller can encode a direct queue selection in the
                  // Play frame itself (state index / playing_track_index) without a
                  // new Load. Resolve that index against the retained field-27 queue.
                  uint32_t requestedIndex = trackRefIndex_;
                  bool explicitSelection = false;
                  if (info.hasPlayingTrackIndex) {
                    requestedIndex = info.playingTrackIndex;
                    explicitSelection = true;
                  } else if (info.hasStateIndex) {
                    requestedIndex = info.stateIndex;
                    explicitSelection = true;
                  }
                  if (explicitSelection && requestedIndex < spircStateTrackRefs_.size() &&
                      requestedIndex != trackRefIndex_) {
                    ++spircPlaySelectFrames_;
                    ++spircPlaySelectByIndex_;
                    SpircFrameInfo selected = makeRetainedSpircState(requestedIndex, 1u, 0u);
                    if (!sendSpircControlNotify(selected, "reset-track-change")) { tcp.stop(); state_ = State::Failed; return false; }
                    if (!sendTrackMetadataRequest(selected)) { tcp.stop(); state_ = State::Failed; return false; }
                  } else {
                    SpircFrameInfo current = makeRetainedSpircState(trackRefIndex_, 1u, currentSpircPositionMs());
                    if (!sendSpircControlNotify(current, "play-current")) { tcp.stop(); state_ = State::Failed; return false; }
                  }
                }
              } else if (info.type == SPIRC_PAUSE) {
                ++spircPauseFrames_;
                if (!self) {
                  SpircFrameInfo current = makeRetainedSpircState(trackRefIndex_, 2u, currentSpircPositionMs());
                  if (!sendSpircControlNotify(current, "pause-current")) { tcp.stop(); state_ = State::Failed; return false; }
                }
              } else if (info.type == SPIRC_PLAY_PAUSE) {
                ++spircPlayPauseFrames_;
                if (!self) {
                  const uint32_t status = spircLastLoadStatus_ == 1u ? 2u : 1u;
                  SpircFrameInfo current = makeRetainedSpircState(trackRefIndex_, status, currentSpircPositionMs());
                  if (!sendSpircControlNotify(current, "playpause-current")) { tcp.stop(); state_ = State::Failed; return false; }
                }
              } else if (info.type == SPIRC_SEEK) {
                ++spircSeekFrames_;
                if (!self) {
                  const uint32_t position = info.hasPosition ? info.position :
                                            (info.hasPositionMs ? info.positionMs : currentSpircPositionMs());
                  SpircFrameInfo current = makeRetainedSpircState(trackRefIndex_,
                      spircLastLoadStatus_ == 1u ? 1u : 2u, position);
                  if (!sendSpircControlNotify(current, "seek-explicit")) { tcp.stop(); state_ = State::Failed; return false; }
                }
              } else if (info.type == SPIRC_NEXT || info.type == SPIRC_PREV) {
                if (info.type == SPIRC_NEXT) ++spircNextFrames_; else ++spircPrevFrames_;
                if (!self && !spircStateTrackRefs_.empty()) {
                  uint32_t nextIndex = trackRefIndex_;
                  if (info.type == SPIRC_NEXT) {
                    if (nextIndex + 1u < spircStateTrackRefs_.size()) ++nextIndex;
                  } else if (nextIndex != 0u) {
                    --nextIndex;
                  }
                  const bool trackChanged = nextIndex != trackRefIndex_;
                  SpircFrameInfo nextState = makeRetainedSpircState(nextIndex,
                      spircLastLoadStatus_ == 1u ? 1u : 2u,
                      trackChanged ? 0u : currentSpircPositionMs());
                  if (!sendSpircControlNotify(nextState,
                          trackChanged ? "reset-track-change" : "nav-boundary-current")) {
                    tcp.stop(); state_ = State::Failed; return false;
                  }
                  if (trackChanged && !sendTrackMetadataRequest(nextState)) {
                    tcp.stop(); state_ = State::Failed; return false;
                  }
                }
              }
              state_ = State::SpircReady;
            }
          }
        } else {
          ++mercuryResponses_;
          if (liveCommand == MERCURY_SUB_COMMAND && sequence == mercurySubscriptionSequence_) {
            ++mercurySubResponses_;
            subscriptionReadyThisSession = true;
            state_ = State::SessionActive;
            subscriptionBecameReady = true;
          }
          if (liveCommand == MERCURY_SEND_COMMAND &&
              sequence == spircHelloMercurySequence_) {
            ++spircHelloAcks_;
            state_ = State::SpircReady;
          }
          if (liveCommand == MERCURY_SEND_COMMAND &&
              sequence == spircTransferNotifyMercurySequence_) {
            ++spircTransferNotifyAcks_;
            state_ = State::SpircReady;
          }
          if (liveCommand == MERCURY_SEND_COMMAND &&
              sequence == spircBlockedNotifyMercurySequence_) {
            ++spircBlockedNotifyAcks_;
            state_ = State::SpircReady;
          }
          if (liveCommand == MERCURY_SEND_COMMAND &&
              sequence == spircControlNotifyMercurySequence_) {
            ++spircControlNotifyAcks_;
            state_ = State::SpircReady;
          }
          if (liveCommand == MERCURY_SEND_COMMAND &&
              sequence == metadataMercurySequence_) {
            ++metadataResponses_;
            if (metadataRequestedAtMs_ != 0u) {
              metadataLastRoundTripMs_ = millis() - metadataRequestedAtMs_;
              if (metadataLastRoundTripMs_ > metadataMaxRoundTripMs_) {
                metadataMaxRoundTripMs_ = metadataLastRoundTripMs_;
              }
              metadataRequestedAtMs_ = 0u;
            }
            metadataLastStatus_ = mercuryStatus;
            metadataLastBytes_ = parts.empty() ? 0u : parts.front().size();
            if (mercuryStatus == 200 && !parts.empty()) {
              auditMetadataPayload(parts.front());
              LegacyTrackMetadataInfo metadata;
              if (parseLegacyTrackMetadata(parts.front(), metadata)) {
                ++metadataSuccesses_;
                strlcpy(metadataTitle_, metadata.title.c_str(), sizeof(metadataTitle_));
                strlcpy(metadataArtists_, metadata.artists.c_str(), sizeof(metadataArtists_));
                strlcpy(metadataAlbum_, metadata.album.c_str(), sizeof(metadataAlbum_));
                metadataDurationMs_ = metadata.durationMs;
                metadataPlaybackGeneration_ = metadataAuditGeneration_;
                metadataCoverCount_ = metadata.coverCount;
                const String coverHex = metadata.coverId.empty() ? String() :
                    bytesToHex(metadata.coverId.data(), metadata.coverId.size());
                strlcpy(metadataCoverIdHex_, coverHex.c_str(), sizeof(metadataCoverIdHex_));
                metadataAudioFileCount_ = metadata.audioFileCount;
                metadataPreferredFormat_ = metadata.preferredFormat;
                const String audioHex = metadata.preferredFileId.empty() ? String() :
                    bytesToHex(metadata.preferredFileId.data(), metadata.preferredFileId.size());
                strlcpy(metadataPreferredFileIdHex_, audioHex.c_str(), sizeof(metadataPreferredFileIdHex_));
                mediaHeadFetchedForTrack_ = false;
                mediaHeadHttpCode_ = 0;
                mediaHeadContentLength_ = -1;
                mediaHeadBytes_ = 0u;
                mediaHeadRangeHonored_ = false;
                mediaHeadOggCapture_ = false;
                setMediaHeadError("none");
                clearAudioKeyCandidates();
                auto addAudioKeyCandidate = [&](const LegacyTrackMetadataInfo::AudioFileCandidate& candidate) {
                  if (candidate.fileId.size() != AUDIO_FILE_ID_BYTES) return;
                  for (uint8_t i = 0u; i < audioKeyCandidateCount_; ++i) {
                    if (memcmp(audioKeyCandidateFileIds_[i], candidate.fileId.data(), AUDIO_FILE_ID_BYTES) == 0) {
                      return;
                    }
                  }
                  if (audioKeyCandidateCount_ >= MAX_AUDIO_KEY_CANDIDATES) {
                    ++audioKeyCandidateTruncated_;
                    return;
                  }
                  const uint8_t index = audioKeyCandidateCount_++;
                  memcpy(audioKeyCandidateFileIds_[index], candidate.fileId.data(), AUDIO_FILE_ID_BYTES);
                  audioKeyCandidateFormats_[index] = candidate.format;
                };

                // Keep dev.2i-r1 preference semantics: OGG_VORBIS_160 (format 1)
                // is first when present. The remaining valid file ids are diagnostic
                // fallbacks in their metadata order. Duplicate ids are ignored.
                if (metadata.preferredFileId.size() == AUDIO_FILE_ID_BYTES) {
                  for (const auto& candidate : metadata.audioFiles) {
                    if (candidate.fileId == metadata.preferredFileId) {
                      addAudioKeyCandidate(candidate);
                      break;
                    }
                  }
                }
                for (const auto& candidate : metadata.audioFiles) addAudioKeyCandidate(candidate);

                if (audioKeyCandidateCount_ != 0u) {
                  if (mediaKeyServiceBlocked_) {
                    ++mediaKeySuppressedTracks_;
                    audioKeyCandidateIndex_ = 0u;
                    memcpy(selectedAudioFileId_, audioKeyCandidateFileIds_[0], AUDIO_FILE_ID_BYTES);
                    recordMetadataKeyTarget(selectedTrackGid_, selectedAudioFileId_, 0u, false);
                    setError("Spotify media key service-blocked; RequestKey suppressed");
                    if (!sendSpircBlockedNotify()) {
                      tcp.stop(); state_ = State::Failed; return false;
                    }
                    if (!startApStreamCanary()) {
                      tcp.stop(); state_ = State::Failed; return false;
                    }
                  } else {
                    if (!sendAudioKeyCandidate(0u)) {
                      tcp.stop(); state_ = State::Failed; return false;
                    }
                    setError("none");
                  }
                } else {
                  ++audioKeyErrors_;
                  ++audioKeyProtocolErrors_;
                  setError("track metadata has no valid audio file id");
                }
              } else {
                ++metadataParseFailures_;
                setError("track metadata protobuf parse failed");
              }
            } else {
              ++metadataParseFailures_;
              metadataAuditHttpError();
              setError("track metadata Mercury response not 200");
            }
            metadataMercurySequence_ = ~static_cast<uint64_t>(0);
            state_ = State::SpircReady;
          }
        }

        if (subscriptionBecameReady && !spircHelloSentThisSession) {
          if (!sendSpircHello()) { tcp.stop(); state_ = State::Failed; return false; }
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
    closeKeyProbeSession(stopRequested_ ? spotify_key_probe::Reason::SessionStop
                                        : spotify_key_probe::Reason::SessionClosed);
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
