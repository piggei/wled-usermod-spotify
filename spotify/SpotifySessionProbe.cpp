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
constexpr const char* SPIRC_PROTOCOL_VERSION = "2.7.1";
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
  addIntCapability(13u, 1);  // kSupportsPlaylistV2
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
  uint32_t playingTrackIndex = 0u;
  bool hasPlayingTrackIndex = false;
  uint32_t trackCount = 0u;
  uint32_t selectedTrackIndex = 0u;
  std::vector<uint8_t> selectedTrackGid;
  String selectedTrackUri;
};

bool parseSpircFrame(const std::vector<uint8_t>& data, SpircFrameInfo& info) {
  uint64_t type = 0u;
  if (!extractProtoVarint(data.data(), data.size(), 5u, type)) return false;
  info.type = static_cast<uint32_t>(type);
  extractProtoString(data.data(), data.size(), 2u, info.ident);

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
    if (extractProtoVarint(state.data(), state.size(), 26u, value)) {
      info.playingTrackIndex = static_cast<uint32_t>(value);
      info.hasPlayingTrackIndex = true;
    }

    const uint32_t wantedTrack = info.hasPlayingTrackIndex ? info.playingTrackIndex : 0u;
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

std::vector<uint8_t> buildSpircTransferNotify(uint32_t sequence,
                                              const char* deviceId,
                                              const char* deviceName,
                                              uint16_t volume,
                                              uint64_t syncedTimestampMs,
                                              const SpircFrameInfo& remote) {
  std::vector<uint8_t> state;
  if (remote.contextUri.length() != 0u) appendStringField(state, 2u, remote.contextUri);
  const uint32_t position = remote.hasPosition ? remote.position :
                            (remote.hasPositionMs ? remote.positionMs : 0u);
  appendVarintField(state, 4u, position);
  // cspot marks the receiver active and Playing immediately when accepting a Load,
  // then sends Notify before track acquisition. Mirror that activation contract.
  appendVarintField(state, 5u, 1u);
  appendVarintField(state, 7u, syncedTimestampMs);
  appendVarintField(state, 13u, 0u);
  appendVarintField(state, 14u, 0u);
  if (remote.hasPlayingTrackIndex) appendVarintField(state, 26u, remote.playingTrackIndex);

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
  metadataMercurySequence_ = ~static_cast<uint64_t>(0);
  metadataLastStatus_ = 0;
  metadataLastBytes_ = 0u;
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
  memset(audioKey_, 0, sizeof(audioKey_));
  productInfoBytes_ = 0u;
  productInfoHash_ = 0u;
  productInfoXmlLike_ = false;
  strlcpy(productInfoType_, "none", sizeof(productInfoType_));
  strlcpy(productInfoCatalogue_, "none", sizeof(productInfoCatalogue_));
  strlcpy(productInfoPlayerLicense_, "none", sizeof(productInfoPlayerLicense_));
  strlcpy(productInfoHeadFiles_, "none", sizeof(productInfoHeadFiles_));
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
  apStreamCandidateFormat_ = -1;
  apStreamHeadersComplete_ = false;
  apStreamPending_ = false;
  apStreamAttemptedForTrack_ = false;
  setApStreamError("none");
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
  spircLastType_ = 0u;
  spircRemoteActive_ = false;
  spircLocalActive_ = false;
  spircTransferNotifyMercurySequence_ = ~static_cast<uint64_t>(0);
  spircTransferNotifyAttempts_ = 0u;
  spircTransferNotifySent_ = 0u;
  spircTransferNotifyAcks_ = 0u;
  spircTransferNotifyBytes_ = 0u;
  spircLastLoadTrackCount_ = 0u;
  spircLastLoadPositionMs_ = 0u;
  spircLastLoadStatus_ = 0u;
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
  productInfoPackets_ = 0u;
  productInfoBytes_ = 0u;
  productInfoHash_ = 0u;
  productInfoXmlLike_ = false;
  strlcpy(productInfoType_, "none", sizeof(productInfoType_));
  strlcpy(productInfoCatalogue_, "none", sizeof(productInfoCatalogue_));
  strlcpy(productInfoPlayerLicense_, "none", sizeof(productInfoPlayerLicense_));
  strlcpy(productInfoHeadFiles_, "none", sizeof(productInfoHeadFiles_));
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
  apStreamTrackChangeCancels_ = 0u;
  apStreamNextChannelId_ = 0u;
  apStreamChannelId_ = 0u;
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
  apStreamCandidateFormat_ = -1;
  apStreamHeadersComplete_ = false;
  apStreamPending_ = false;
  apStreamAttemptedForTrack_ = false;
  setApStreamError("none");
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
  apStreamCandidateFormat_ = -1;
  apStreamHeadersComplete_ = false;
  apStreamPending_ = false;
  apStreamAttemptedForTrack_ = false;
  setApStreamError("none");
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

  auto sendSpircTransferNotify = [&](const SpircFrameInfo& remote) -> bool {
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
    spircLocalActive_ = true;
    spircLastLoadTrackCount_ = remote.trackCount;
    spircLastLoadPositionMs_ = remote.hasPosition ? remote.position :
                               (remote.hasPositionMs ? remote.positionMs : 0u);
    spircLastLoadStatus_ = remote.hasPlayStatus ? remote.playStatus : 0u;
    strlcpy(spircLastLoadContext_, remote.contextUri.c_str(), sizeof(spircLastLoadContext_));
    state_ = State::SpircReady;
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
      apStreamCandidateFormat_ = -1;
      apStreamHeadersComplete_ = false;
      apStreamPending_ = false;
      apStreamAttemptedForTrack_ = false;
      setApStreamError("none");
    }

    trackRefIndex_ = remote.selectedTrackIndex;
    strlcpy(trackRefGidHex_, gidHex.c_str(), sizeof(trackRefGidHex_));
    strlcpy(trackRefUri_, remote.selectedTrackUri.c_str(), sizeof(trackRefUri_));
    memcpy(selectedTrackGid_, remote.selectedTrackGid.data(), TRACK_GID_BYTES);
    memset(selectedAudioFileId_, 0, sizeof(selectedAudioFileId_));
    memset(audioKey_, 0, sizeof(audioKey_));
    audioKeyBytes_ = 0u;
    audioKeyLastCommand_ = 0u;
    audioKeyError0_ = 0u;
    audioKeyError1_ = 0u;
    const String metadataUri = String(TRACK_METADATA_PREFIX) + gidHex;
    metadataMercurySequence_ = mercurySequence_++;
    const std::vector<uint8_t> request = buildMercuryRequest(
        metadataMercurySequence_, String(F("GET")), metadataUri);
    ++metadataRequests_;
    if (request.empty() ||
        !sendShannonPacket(tcp, sendCipher, sendNonce, MERCURY_SEND_COMMAND, request, IO_TIMEOUT_MS)) {
      metadataMercurySequence_ = ~static_cast<uint64_t>(0);
      setError("track metadata Mercury GET failed");
      return false;
    }
    ++txPackets_;
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

  auto startApStreamCanary = [&]() -> bool {
    if (apStreamAttemptedForTrack_) return true;
    apStreamAttemptedForTrack_ = true;
    ++apStreamAttempts_;
    apStreamChannelId_ = apStreamNextChannelId_++;
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
    apStreamHeadersComplete_ = false;
    apStreamPending_ = false;
    setApStreamError("none");

    if (audioKeyCandidateCount_ == 0u) {
      ++apStreamProtocolErrors_;
      setApStreamError("no AudioFile candidate for AP stream");
      return true;
    }
    apStreamCandidateFormat_ = audioKeyCandidateFormats_[0];
    const std::vector<uint8_t> request = buildApStreamChunkRequest(
        apStreamChannelId_, audioKeyCandidateFileIds_[0], 0u, AP_STREAM_CANARY_WORDS);
    apStreamRequestBytes_ = request.size();
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

  while (!stopRequested_) {
    if (WiFi.status() != WL_CONNECTED) {
      tcp.stop(); state_ = State::Failed; setError("WiFi lost during Spotify session"); return false;
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
      if (!apStreamAttemptedForTrack_ || channelId != apStreamChannelId_ || !apStreamPending_) {
        ++apStreamStalePackets_;
        continue;
      }

      if (liveCommand == STREAM_CHUNK_FAILURE_COMMAND) {
        apStreamPending_ = false;
        apStreamRequestedAtMs_ = 0u;
        ++apStreamFailures_;
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
        setApStreamError("AP StreamChunk header framing invalid");
        continue;
      }

      if (apStreamHeadersComplete_) {
        if (offset < payload.size()) {
          ++apStreamDataPackets_;
          apStreamDataBytes_ += payload.size() - offset;
          if (apStreamDataBytes_ >= AP_STREAM_CANARY_BYTES) {
            apStreamPending_ = false;
            apStreamRequestedAtMs_ = 0u;
            ++apStreamSuccesses_;
            setApStreamError("none");
          }
        } else if (wasDataState) {
          apStreamPending_ = false;
          apStreamRequestedAtMs_ = 0u;
          if (apStreamDataBytes_ != 0u) {
            ++apStreamSuccesses_;
            setApStreamError("none");
          } else {
            ++apStreamProtocolErrors_;
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
        setError(hasNext ? "Spotify AesKeyError; trying next audio file" :
                           "Spotify AesKeyError for all audio files");
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
              if (info.type == SPIRC_NOTIFY) {
                ++spircNotifyFrames_;
              } else if (info.type == SPIRC_LOAD) {
                ++spircLoadFrames_;
                // A remote Load is the Connect transfer request. cspot accepts it by
                // becoming active and immediately sending a Notify containing the
                // transferred context/position. Playback acquisition remains outside
                // this diagnostic gate, but the control-plane acknowledgement is real.
                if (!self && !sendSpircTransferNotify(info)) {
                  tcp.stop(); state_ = State::Failed; return false;
                }
                if (!self && !sendTrackMetadataRequest(info)) {
                  tcp.stop(); state_ = State::Failed; return false;
                }
              } else if (info.type == SPIRC_PLAY) {
                ++spircPlayFrames_;
              } else if (info.type == SPIRC_PAUSE) {
                ++spircPauseFrames_;
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
              sequence == metadataMercurySequence_) {
            ++metadataResponses_;
            metadataLastStatus_ = mercuryStatus;
            metadataLastBytes_ = parts.empty() ? 0u : parts.front().size();
            if (mercuryStatus == 200 && !parts.empty()) {
              LegacyTrackMetadataInfo metadata;
              if (parseLegacyTrackMetadata(parts.front(), metadata)) {
                ++metadataSuccesses_;
                strlcpy(metadataTitle_, metadata.title.c_str(), sizeof(metadataTitle_));
                strlcpy(metadataArtists_, metadata.artists.c_str(), sizeof(metadataArtists_));
                strlcpy(metadataAlbum_, metadata.album.c_str(), sizeof(metadataAlbum_));
                metadataDurationMs_ = metadata.durationMs;
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
                  if (!sendAudioKeyCandidate(0u)) {
                    tcp.stop(); state_ = State::Failed; return false;
                  }
                  setError("none");
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
