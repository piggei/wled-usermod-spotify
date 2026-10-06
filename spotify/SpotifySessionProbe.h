#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <vector>

// dev.2l-r1 bounded AP StreamChunk canary.
//
// Qualified dev.2i-r2 metadata/audio-key behavior is retained unchanged. dev.2j-r2
// proved that ProductInfo carries head-files=0 for this account, and dev.2k-r1
// proved on the real WLED target that the prebuilt framework lacks the mbedTLS TLS
// engine required by esp_http_client/esp-tls. dev.2l therefore tests the historical
// AP media channel already available inside the authenticated Shannon session. It
// requests only 4 KiB of the preferred encrypted AudioFile and discards data after
// counting it. No AES decrypt, decoder, CDN, Login5 or alternate TLS stack is added.
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
    SpircAdvertising,
    SpircReady,
    Reconnecting,
    Stopping,
    AuthDeclined,
    Failed
  };

  void begin();
  void loop(bool enabled, bool credentialsReady,
            const String& userName, uint8_t authType,
            const std::vector<uint8_t>& authData, const char* deviceId,
            const char* deviceName, uint8_t volumePercent);
  bool startNow(const String& userName, uint8_t authType,
                const std::vector<uint8_t>& authData, const char* deviceId,
                const char* deviceName, uint8_t volumePercent);
  void requestStop();
  void reset();

  bool active() const { return task_ != nullptr; }
  bool authenticated() const {
    return state_ == State::Authenticated || state_ == State::MercurySubscribing ||
           state_ == State::SessionActive || state_ == State::SpircAdvertising ||
           state_ == State::SpircReady;
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
  uint32_t spircUriRootEvents() const { return spircUriRootEvents_; }
  uint32_t spircUriChildEvents() const { return spircUriChildEvents_; }
  const char* lastReadStage() const { return lastReadStage_; }
  size_t lastReadDeclaredPayload() const { return lastReadDeclaredPayload_; }
  uint32_t oversizedPackets() const { return oversizedPackets_; }

  uint32_t spircHelloAttempts() const { return spircHelloAttempts_; }
  uint32_t spircHelloSent() const { return spircHelloSent_; }
  uint32_t spircHelloAcks() const { return spircHelloAcks_; }
  size_t spircHelloBytes() const { return spircHelloBytes_; }
  uint32_t spircRxFrames() const { return spircRxFrames_; }
  uint32_t spircRemoteFrames() const { return spircRemoteFrames_; }
  uint32_t spircSelfEchoes() const { return spircSelfEchoes_; }
  uint32_t spircNotifyFrames() const { return spircNotifyFrames_; }
  uint32_t spircLoadFrames() const { return spircLoadFrames_; }
  uint32_t spircPlayFrames() const { return spircPlayFrames_; }
  uint32_t spircPauseFrames() const { return spircPauseFrames_; }
  uint32_t spircLastType() const { return spircLastType_; }
  bool spircRemoteActive() const { return spircRemoteActive_; }
  bool spircLocalActive() const { return spircLocalActive_; }
  uint32_t spircTransferNotifyAttempts() const { return spircTransferNotifyAttempts_; }
  uint32_t spircTransferNotifySent() const { return spircTransferNotifySent_; }
  uint32_t spircTransferNotifyAcks() const { return spircTransferNotifyAcks_; }
  size_t spircTransferNotifyBytes() const { return spircTransferNotifyBytes_; }
  uint32_t spircLastLoadTrackCount() const { return spircLastLoadTrackCount_; }
  uint32_t spircLastLoadPositionMs() const { return spircLastLoadPositionMs_; }
  uint32_t spircLastLoadStatus() const { return spircLastLoadStatus_; }
  const char* spircLastLoadContext() const { return spircLastLoadContext_; }
  const char* spircRemoteIdent() const { return spircRemoteIdent_; }
  const char* spircRemoteName() const { return spircRemoteName_; }

  uint32_t trackRefIndex() const { return trackRefIndex_; }
  const char* trackRefGidHex() const { return trackRefGidHex_; }
  const char* trackRefUri() const { return trackRefUri_; }
  uint32_t metadataRequests() const { return metadataRequests_; }
  uint32_t metadataResponses() const { return metadataResponses_; }
  uint32_t metadataSuccesses() const { return metadataSuccesses_; }
  uint32_t metadataParseFailures() const { return metadataParseFailures_; }
  int32_t metadataLastStatus() const { return metadataLastStatus_; }
  size_t metadataLastBytes() const { return metadataLastBytes_; }
  const char* metadataTitle() const { return metadataTitle_; }
  const char* metadataArtists() const { return metadataArtists_; }
  const char* metadataAlbum() const { return metadataAlbum_; }
  uint32_t metadataDurationMs() const { return metadataDurationMs_; }
  uint32_t metadataCoverCount() const { return metadataCoverCount_; }
  const char* metadataCoverIdHex() const { return metadataCoverIdHex_; }
  uint32_t metadataAudioFileCount() const { return metadataAudioFileCount_; }
  int32_t metadataPreferredFormat() const { return metadataPreferredFormat_; }
  const char* metadataPreferredFileIdHex() const { return metadataPreferredFileIdHex_; }

  uint32_t audioKeyRequests() const { return audioKeyRequests_; }
  uint32_t audioKeyResponses() const { return audioKeyResponses_; }
  uint32_t audioKeySuccesses() const { return audioKeySuccesses_; }
  uint32_t audioKeyErrors() const { return audioKeyErrors_; }
  uint32_t audioKeyTimeouts() const { return audioKeyTimeouts_; }
  uint32_t audioKeyServiceRejects() const { return audioKeyServiceRejects_; }
  uint32_t audioKeyProtocolErrors() const { return audioKeyProtocolErrors_; }
  uint32_t audioKeyStaleResponses() const { return audioKeyStaleResponses_; }
  uint32_t audioKeyTrackChangeCancels() const { return audioKeyTrackChangeCancels_; }
  bool audioKeyPending() const { return audioKeyPending_; }
  uint32_t audioKeyLastSequence() const { return audioKeyLastSequence_; }
  size_t audioKeyRequestBytes() const { return audioKeyRequestBytes_; }
  size_t audioKeyBytes() const { return audioKeyBytes_; }
  uint8_t audioKeyLastCommand() const { return audioKeyLastCommand_; }
  uint8_t audioKeyError0() const { return audioKeyError0_; }
  uint8_t audioKeyError1() const { return audioKeyError1_; }
  uint8_t audioKeyCandidateCount() const { return audioKeyCandidateCount_; }
  uint8_t audioKeyCandidateIndex() const { return audioKeyCandidateIndex_; }
  int32_t audioKeyCandidateFormat() const {
    return audioKeyCandidateIndex_ < audioKeyCandidateCount_
               ? audioKeyCandidateFormats_[audioKeyCandidateIndex_]
               : -1;
  }
  uint32_t audioKeyCandidateAdvances() const { return audioKeyCandidateAdvances_; }
  uint32_t audioKeyCandidateTruncated() const { return audioKeyCandidateTruncated_; }
  int32_t audioKeyCandidateFormatAt(uint8_t index) const {
    return index < audioKeyCandidateCount_ ? audioKeyCandidateFormats_[index] : -1;
  }
  uint8_t audioKeyCandidateResultCommandAt(uint8_t index) const {
    return index < audioKeyCandidateCount_ ? audioKeyCandidateResultCommand_[index] : 0u;
  }
  uint8_t audioKeyCandidateError0At(uint8_t index) const {
    return index < audioKeyCandidateCount_ ? audioKeyCandidateError0_[index] : 0u;
  }
  uint8_t audioKeyCandidateError1At(uint8_t index) const {
    return index < audioKeyCandidateCount_ ? audioKeyCandidateError1_[index] : 0u;
  }
  bool audioKeyCandidateTimedOutAt(uint8_t index) const {
    return index < audioKeyCandidateCount_ && audioKeyCandidateTimedOut_[index];
  }

  uint32_t productInfoPackets() const { return productInfoPackets_; }
  size_t productInfoBytes() const { return productInfoBytes_; }
  uint32_t productInfoHash() const { return productInfoHash_; }
  bool productInfoXmlLike() const { return productInfoXmlLike_; }
  const char* productInfoType() const { return productInfoType_; }
  const char* productInfoCatalogue() const { return productInfoCatalogue_; }
  const char* productInfoPlayerLicense() const { return productInfoPlayerLicense_; }
  const char* productInfoHeadFiles() const { return productInfoHeadFiles_; }
  bool headFileTemplateAvailable() const { return headFileTemplate_[0] != '\0'; }
  const char* headFileScheme() const { return headFileScheme_; }
  uint32_t mediaHeadAttempts() const { return mediaHeadAttempts_; }
  uint32_t mediaHeadSuccesses() const { return mediaHeadSuccesses_; }
  uint32_t mediaHeadSkipped() const { return mediaHeadSkipped_; }
  int mediaHeadHttpCode() const { return mediaHeadHttpCode_; }
  int32_t mediaHeadContentLength() const { return mediaHeadContentLength_; }
  size_t mediaHeadBytes() const { return mediaHeadBytes_; }
  bool mediaHeadRangeHonored() const { return mediaHeadRangeHonored_; }
  bool mediaHeadOggCapture() const { return mediaHeadOggCapture_; }
  uint32_t mediaHeadUnsupportedScheme() const { return mediaHeadUnsupportedScheme_; }
  const char* mediaHeadLastError() const { return mediaHeadLastError_; }

  uint32_t apStreamAttempts() const { return apStreamAttempts_; }
  uint32_t apStreamSuccesses() const { return apStreamSuccesses_; }
  uint32_t apStreamFailures() const { return apStreamFailures_; }
  uint32_t apStreamTimeouts() const { return apStreamTimeouts_; }
  uint32_t apStreamProtocolErrors() const { return apStreamProtocolErrors_; }
  uint32_t apStreamStalePackets() const { return apStreamStalePackets_; }
  uint32_t apStreamTrackChangeCancels() const { return apStreamTrackChangeCancels_; }
  bool apStreamPending() const { return apStreamPending_; }
  uint16_t apStreamChannelId() const { return apStreamChannelId_; }
  size_t apStreamRequestBytes() const { return apStreamRequestBytes_; }
  uint32_t apStreamRequestedBytes() const { return AP_STREAM_CANARY_BYTES; }
  uint32_t apStreamResponsePackets() const { return apStreamResponsePackets_; }
  uint8_t apStreamLastCommand() const { return apStreamLastCommand_; }
  uint16_t apStreamFailureCode() const { return apStreamFailureCode_; }
  uint32_t apStreamHeaderCount() const { return apStreamHeaderCount_; }
  size_t apStreamHeaderBytes() const { return apStreamHeaderBytes_; }
  uint32_t apStreamReportedFileBytes() const { return apStreamReportedFileBytes_; }
  uint32_t apStreamDataPackets() const { return apStreamDataPackets_; }
  size_t apStreamDataBytes() const { return apStreamDataBytes_; }
  bool apStreamHeadersComplete() const { return apStreamHeadersComplete_; }
  int32_t apStreamCandidateFormat() const { return apStreamCandidateFormat_; }
  const char* apStreamLastError() const { return apStreamLastError_; }

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
  static constexpr uint32_t AUDIO_KEY_TIMEOUT_MS = 2500u;
  static constexpr uint8_t MAX_AUDIO_KEY_CANDIDATES = 8u;
  static constexpr size_t MEDIA_HEAD_MAX_BYTES = 4096u;
  static constexpr uint32_t MEDIA_HEAD_TIMEOUT_MS = 5000u;
  static constexpr size_t AP_STREAM_CANARY_BYTES = 4096u;
  static constexpr uint32_t AP_STREAM_WORD_BYTES = 4u;
  static constexpr uint32_t AP_STREAM_CANARY_WORDS = AP_STREAM_CANARY_BYTES / AP_STREAM_WORD_BYTES;
  static constexpr uint32_t AP_STREAM_TIMEOUT_MS = 5000u;
  static constexpr uint32_t MAX_AUTO_RECONNECTS = 5u;
  static constexpr size_t MAX_AP_PLAIN_PACKET = 16384u;
  static constexpr size_t MAX_AP_ENCRYPTED_PACKET = 16384u;

  static void taskThunk(void* arg);
  void taskLoop();
  bool runOneSession(bool reconnecting);
  bool resolveAccessPoint(String& endpoint);
  bool resolveWithHttp(String& endpoint);
  static bool extractFirstEndpoint(const String& json, const char* key, String& endpoint);
  static bool extractXmlTag(const std::vector<uint8_t>& payload, const char* tag, String& value);
  static bool splitEndpoint(const String& endpoint, String& host, uint16_t& port);
  bool fetchMediaHeadCandidate(uint8_t candidateIndex);
  void setMediaHeadError(const char* text);
  void setApStreamError(const char* text);
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
  char credentialDeviceName_[33] = {0};
  uint16_t credentialVolume16_ = 0u;

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
  uint32_t serverTimestampLocalMs_ = 0u;
  char countryCode_[3] = {0};

  uint64_t mercurySequence_ = 0u;
  uint64_t mercurySubscriptionSequence_ = ~static_cast<uint64_t>(0);
  uint32_t mercurySubAttempts_ = 0u;
  uint32_t mercurySubResponses_ = 0u;
  uint32_t mercuryResponses_ = 0u;
  uint32_t mercuryEvents_ = 0u;
  uint64_t mercuryLastSequence_ = 0u;
  char mercuryLastUri_[128] = {0};
  uint32_t spircUriRootEvents_ = 0u;
  uint32_t spircUriChildEvents_ = 0u;
  char lastReadStage_[16] = "none";
  size_t lastReadDeclaredPayload_ = 0u;
  uint32_t oversizedPackets_ = 0u;

  uint32_t spircSequence_ = 0u;
  uint64_t spircHelloMercurySequence_ = ~static_cast<uint64_t>(0);
  uint32_t spircHelloAttempts_ = 0u;
  uint32_t spircHelloSent_ = 0u;
  uint32_t spircHelloAcks_ = 0u;
  size_t spircHelloBytes_ = 0u;
  uint32_t spircRxFrames_ = 0u;
  uint32_t spircRemoteFrames_ = 0u;
  uint32_t spircSelfEchoes_ = 0u;
  uint32_t spircNotifyFrames_ = 0u;
  uint32_t spircLoadFrames_ = 0u;
  uint32_t spircPlayFrames_ = 0u;
  uint32_t spircPauseFrames_ = 0u;
  uint32_t spircLastType_ = 0u;
  bool spircRemoteActive_ = false;
  bool spircLocalActive_ = false;
  uint64_t spircTransferNotifyMercurySequence_ = ~static_cast<uint64_t>(0);
  uint32_t spircTransferNotifyAttempts_ = 0u;
  uint32_t spircTransferNotifySent_ = 0u;
  uint32_t spircTransferNotifyAcks_ = 0u;
  size_t spircTransferNotifyBytes_ = 0u;
  uint32_t spircLastLoadTrackCount_ = 0u;
  uint32_t spircLastLoadPositionMs_ = 0u;
  uint32_t spircLastLoadStatus_ = 0u;
  char spircLastLoadContext_[96] = {0};
  char spircRemoteIdent_[48] = {0};
  char spircRemoteName_[33] = {0};

  uint64_t metadataMercurySequence_ = ~static_cast<uint64_t>(0);
  uint32_t trackRefIndex_ = 0u;
  char trackRefGidHex_[33] = {0};
  char trackRefUri_[96] = {0};
  uint32_t metadataRequests_ = 0u;
  uint32_t metadataResponses_ = 0u;
  uint32_t metadataSuccesses_ = 0u;
  uint32_t metadataParseFailures_ = 0u;
  int32_t metadataLastStatus_ = 0;
  size_t metadataLastBytes_ = 0u;
  char metadataTitle_[96] = {0};
  char metadataArtists_[128] = {0};
  char metadataAlbum_[96] = {0};
  uint32_t metadataDurationMs_ = 0u;
  uint32_t metadataCoverCount_ = 0u;
  char metadataCoverIdHex_[41] = {0};
  uint32_t metadataAudioFileCount_ = 0u;
  int32_t metadataPreferredFormat_ = -1;
  char metadataPreferredFileIdHex_[41] = {0};

  uint8_t selectedTrackGid_[16] = {0};
  uint8_t selectedAudioFileId_[20] = {0};
  uint8_t audioKey_[16] = {0};
  uint8_t audioKeyCandidateFileIds_[MAX_AUDIO_KEY_CANDIDATES][20] = {{0}};
  int32_t audioKeyCandidateFormats_[MAX_AUDIO_KEY_CANDIDATES] = {0};
  uint8_t audioKeyCandidateResultCommand_[MAX_AUDIO_KEY_CANDIDATES] = {0};
  uint8_t audioKeyCandidateError0_[MAX_AUDIO_KEY_CANDIDATES] = {0};
  uint8_t audioKeyCandidateError1_[MAX_AUDIO_KEY_CANDIDATES] = {0};
  bool audioKeyCandidateTimedOut_[MAX_AUDIO_KEY_CANDIDATES] = {false};
  uint8_t audioKeyCandidateCount_ = 0u;
  uint8_t audioKeyCandidateIndex_ = 0u;
  uint32_t audioKeyCandidateAdvances_ = 0u;
  uint32_t audioKeyCandidateTruncated_ = 0u;
  uint32_t audioKeyNextSequence_ = 0u;
  uint32_t audioKeyPendingSequence_ = 0u;
  uint32_t audioKeyLastSequence_ = 0u;
  uint32_t audioKeyRequestedAtMs_ = 0u;
  uint32_t audioKeyRequests_ = 0u;
  uint32_t audioKeyResponses_ = 0u;
  uint32_t audioKeySuccesses_ = 0u;
  uint32_t audioKeyErrors_ = 0u;
  uint32_t audioKeyTimeouts_ = 0u;
  uint32_t audioKeyServiceRejects_ = 0u;
  uint32_t audioKeyProtocolErrors_ = 0u;
  uint32_t audioKeyStaleResponses_ = 0u;
  uint32_t audioKeyTrackChangeCancels_ = 0u;
  size_t audioKeyRequestBytes_ = 0u;
  size_t audioKeyBytes_ = 0u;
  uint8_t audioKeyLastCommand_ = 0u;
  uint8_t audioKeyError0_ = 0u;
  uint8_t audioKeyError1_ = 0u;
  bool audioKeyPending_ = false;

  uint32_t productInfoPackets_ = 0u;
  size_t productInfoBytes_ = 0u;
  uint32_t productInfoHash_ = 0u;
  bool productInfoXmlLike_ = false;
  char productInfoType_[20] = "none";
  char productInfoCatalogue_[20] = "none";
  char productInfoPlayerLicense_[20] = "none";
  char productInfoHeadFiles_[12] = "none";
  char headFileTemplate_[192] = {0};
  char headFileScheme_[8] = "none";
  uint32_t mediaHeadAttempts_ = 0u;
  uint32_t mediaHeadSuccesses_ = 0u;
  uint32_t mediaHeadSkipped_ = 0u;
  int mediaHeadHttpCode_ = 0;
  int32_t mediaHeadContentLength_ = -1;
  size_t mediaHeadBytes_ = 0u;
  bool mediaHeadRangeHonored_ = false;
  bool mediaHeadOggCapture_ = false;
  uint32_t mediaHeadUnsupportedScheme_ = 0u;
  bool mediaHeadFetchedForTrack_ = false;
  char mediaHeadLastError_[80] = "none";

  uint32_t apStreamAttempts_ = 0u;
  uint32_t apStreamSuccesses_ = 0u;
  uint32_t apStreamFailures_ = 0u;
  uint32_t apStreamTimeouts_ = 0u;
  uint32_t apStreamProtocolErrors_ = 0u;
  uint32_t apStreamStalePackets_ = 0u;
  uint32_t apStreamTrackChangeCancels_ = 0u;
  uint16_t apStreamNextChannelId_ = 0u;
  uint16_t apStreamChannelId_ = 0u;
  uint32_t apStreamRequestedAtMs_ = 0u;
  size_t apStreamRequestBytes_ = 0u;
  uint32_t apStreamResponsePackets_ = 0u;
  uint8_t apStreamLastCommand_ = 0u;
  uint16_t apStreamFailureCode_ = 0u;
  uint32_t apStreamHeaderCount_ = 0u;
  size_t apStreamHeaderBytes_ = 0u;
  uint32_t apStreamReportedFileBytes_ = 0u;
  uint32_t apStreamDataPackets_ = 0u;
  size_t apStreamDataBytes_ = 0u;
  int32_t apStreamCandidateFormat_ = -1;
  bool apStreamHeadersComplete_ = false;
  bool apStreamPending_ = false;
  bool apStreamAttemptedForTrack_ = false;
  char apStreamLastError_[96] = "none";

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
