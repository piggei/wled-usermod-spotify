#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <vector>

// dev.2f-r1 persistent AP/Shannon + Mercury session gate.
//
// Qualified dev.2e-r2 handshake/authentication is retained. After APWelcome the
// socket stays open, Spotify PING packets are acknowledged, country code is
// captured, and a minimal Mercury SUB is sent for hm://remote/3/user/<user>/.
// No SPIRC, metadata decode, audio keys, track decode or Spotify PCM playback
// are introduced in this gate.
class SpotifySessionProbe {
public:
  enum class State : uint8_t {
    Idle,
    WaitingCredentials,
    WaitingWifi,
    Scheduled,
    Resolving,
    Connecting,
    ClientHello,
    ApHello,
    KeyDerivation,
    Authenticating,
    Authenticated,
    MercurySubscribing,
    SessionActive,
    Reconnecting,
    Stopping,
    AuthDeclined,
    Failed
  };

  void begin();
  void loop(bool enabled, bool credentialsReady,
            const String& userName, uint8_t authType,
            const std::vector<uint8_t>& authData, const char* deviceId);
  bool startNow(const String& userName, uint8_t authType,
                const std::vector<uint8_t>& authData, const char* deviceId);
  void requestStop();
  void reset();

  bool active() const { return task_ != nullptr; }
  bool authenticated() const {
    return state_ == State::Authenticated || state_ == State::MercurySubscribing ||
           state_ == State::SessionActive;
  }
  State state() const { return state_; }
  const char* stateName() const;
  const char* endpoint() const { return endpoint_; }
  const char* resolverMode() const { return resolverMode_; }
  const char* lastError() const { return lastError_; }

  uint32_t attempts() const { return attempts_; }
  uint32_t resolveAttempts() const { return resolveAttempts_; }
  uint32_t resolveSuccesses() const { return resolveSuccesses_; }
  int resolveHttpCode() const { return resolveHttpCode_; }
  size_t resolveResponseBytes() const { return resolveResponseBytes_; }
  uint32_t fallbackUses() const { return fallbackUses_; }
  uint32_t tcpAttempts() const { return tcpAttempts_; }
  uint32_t tcpSuccesses() const { return tcpSuccesses_; }

  uint32_t handshakeAttempts() const { return handshakeAttempts_; }
  uint32_t handshakeSuccesses() const { return handshakeSuccesses_; }
  size_t clientHelloBytes() const { return clientHelloBytes_; }
  size_t apHelloBytes() const { return apHelloBytes_; }
  size_t dhSharedBytes() const { return dhSharedBytes_; }
  size_t challengeResponseBytes() const { return challengeResponseBytes_; }
  size_t shannonSendKeyBytes() const { return shannonSendKeyBytes_; }
  size_t shannonRecvKeyBytes() const { return shannonRecvKeyBytes_; }

  uint32_t authAttempts() const { return authAttempts_; }
  uint32_t authSuccesses() const { return authSuccesses_; }
  uint32_t authDeclines() const { return authDeclines_; }
  size_t authRequestBytes() const { return authRequestBytes_; }
  size_t authResponseBytes() const { return authResponseBytes_; }
  uint8_t authLastCommand() const { return authLastCommand_; }
  uint32_t shannonMacFailures() const { return shannonMacFailures_; }

  uint32_t sessionStarts() const { return sessionStarts_; }
  uint32_t sessionUptimeMs() const;
  uint32_t rxPackets() const { return rxPackets_; }
  uint32_t txPackets() const { return txPackets_; }
  uint8_t lastRxCommand() const { return lastRxCommand_; }
  uint32_t lastRxAgeMs() const;
  uint32_t pingReceived() const { return pingReceived_; }
  uint32_t pongSent() const { return pongSent_; }
  uint32_t serverTimestampSeconds() const { return serverTimestampSeconds_; }
  const char* countryCode() const { return countryCode_; }

  uint32_t mercurySubAttempts() const { return mercurySubAttempts_; }
  uint32_t mercurySubResponses() const { return mercurySubResponses_; }
  uint32_t mercuryResponses() const { return mercuryResponses_; }
  uint32_t mercuryEvents() const { return mercuryEvents_; }
  uint64_t mercuryLastSequence() const { return mercuryLastSequence_; }
  const char* mercuryLastUri() const { return mercuryLastUri_; }

  uint32_t reconnectAttempts() const { return reconnectAttempts_; }
  uint32_t reconnectSuccesses() const { return reconnectSuccesses_; }

  uint32_t lastDurationMs() const;
  uint32_t heapBefore() const { return heapBefore_; }
  uint32_t heapAfter() const { return active() ? ESP.getFreeHeap() : heapAfter_; }
  uint32_t minHeapSeen() const { return ESP.getMinFreeHeap(); }
  UBaseType_t stackMinFree() const { return stackMinFree_; }

private:
  static constexpr uint32_t AUTO_DELAY_MS = 3500u;
  static constexpr uint32_t CONNECT_TIMEOUT_MS = 5000u;
  static constexpr uint32_t IO_TIMEOUT_MS = 7000u;
  static constexpr uint32_t SESSION_POLL_MS = 250u;
  static constexpr uint32_t SESSION_RX_TIMEOUT_MS = 130000u;
  static constexpr uint32_t RECONNECT_DELAY_MS = 2500u;
  static constexpr uint32_t MAX_AUTO_RECONNECTS = 5u;
  static constexpr size_t MAX_AP_PLAIN_PACKET = 16384u;
  static constexpr size_t MAX_AP_ENCRYPTED_PACKET = 8192u;

  static void taskThunk(void* arg);
  void taskLoop();
  bool runOneSession(bool reconnecting);
  bool resolveAccessPoint(String& endpoint);
  bool resolveWithHttp(String& endpoint);
  static bool extractFirstEndpoint(const String& json, const char* key, String& endpoint);
  static bool splitEndpoint(const String& endpoint, String& host, uint16_t& port);
  void setError(const char* text);
  void setEndpoint(const String& endpoint);
  void setResolverMode(const char* mode);
  void finishTask(uint32_t startedMs);
  void updateStackWatermark();

  volatile State state_ = State::Idle;
  TaskHandle_t task_ = nullptr;
  volatile bool stopRequested_ = false;
  bool autoAttempted_ = false;
  uint32_t eligibleSinceMs_ = 0u;
  uint32_t taskStartedMs_ = 0u;

  String credentialUser_;
  std::vector<uint8_t> credentialAuthData_;
  uint8_t credentialAuthType_ = 0u;
  char credentialDeviceId_[48] = {0};

  uint32_t attempts_ = 0u;
  uint32_t resolveAttempts_ = 0u;
  uint32_t resolveSuccesses_ = 0u;
  int resolveHttpCode_ = 0;
  size_t resolveResponseBytes_ = 0u;
  uint32_t fallbackUses_ = 0u;
  uint32_t tcpAttempts_ = 0u;
  uint32_t tcpSuccesses_ = 0u;

  uint32_t handshakeAttempts_ = 0u;
  uint32_t handshakeSuccesses_ = 0u;
  size_t clientHelloBytes_ = 0u;
  size_t apHelloBytes_ = 0u;
  size_t dhSharedBytes_ = 0u;
  size_t challengeResponseBytes_ = 0u;
  size_t shannonSendKeyBytes_ = 0u;
  size_t shannonRecvKeyBytes_ = 0u;

  uint32_t authAttempts_ = 0u;
  uint32_t authSuccesses_ = 0u;
  uint32_t authDeclines_ = 0u;
  size_t authRequestBytes_ = 0u;
  size_t authResponseBytes_ = 0u;
  uint8_t authLastCommand_ = 0u;
  uint32_t shannonMacFailures_ = 0u;

  uint32_t sessionStarts_ = 0u;
  uint32_t sessionConnectedMs_ = 0u;
  uint32_t lastRxMs_ = 0u;
  uint32_t rxPackets_ = 0u;
  uint32_t txPackets_ = 0u;
  uint8_t lastRxCommand_ = 0u;
  uint32_t pingReceived_ = 0u;
  uint32_t pongSent_ = 0u;
  uint32_t serverTimestampSeconds_ = 0u;
  char countryCode_[3] = {0};

  uint64_t mercurySequence_ = 0u;
  uint64_t mercurySubscriptionSequence_ = ~static_cast<uint64_t>(0);
  uint32_t mercurySubAttempts_ = 0u;
  uint32_t mercurySubResponses_ = 0u;
  uint32_t mercuryResponses_ = 0u;
  uint32_t mercuryEvents_ = 0u;
  uint64_t mercuryLastSequence_ = 0u;
  char mercuryLastUri_[128] = {0};

  uint32_t reconnectAttempts_ = 0u;
  uint32_t reconnectSuccesses_ = 0u;

  uint32_t lastDurationMs_ = 0u;
  uint32_t heapBefore_ = 0u;
  uint32_t heapAfter_ = 0u;
  UBaseType_t stackMinFree_ = 0u;

  char endpoint_[96] = {0};
  char resolverMode_[16] = "none";
  char lastError_[96] = "none";
};
