#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <vector>
#include "SpotifyMetadataAudit.h"
#include "SpotifyAudioKeyProbe.h"

#include "../decoder/SpotifyApMediaChunkSource.h"
#include "../decoder/SpotifyApContinuousRing.h"

// dev.2n-r17 adds a bounded 64 KiB continuous encrypted AP transport diagnostic
// above the qualified r16 key comparison. It never decrypts or decodes live bytes.
// dev.2n-r16 added a manual, bounded primary/alternative key comparison. Diagnostic
// results/keys never replace the normal target/latch or feed the live consumer.
// The comments below describe the retained normal-path qualification history.
// dev.2m-r14 conservative AP receive-poll optimization above the hardware-qualified r12
// canonical classic-queue identity gate and r13 timing evidence. r14 does not alter selection semantics:
// GID-only TrackRefs remain canonicalized to spotify:track URIs and context
// track_index remains advisory. It only measures local context resolution/apply
// time and metadata round-trip latency so the observed ~1 s UI delay can be
// localized before any scheduling/polling change.
// r7 keeps the r5/r6 playback semantics, but stops advertising kSupportsPlaylistV2
// because this usermod does not implement Spotify's full playlist-v2 command/state
// contract. Frame.context_player_state is classified as either legacy JSON or the
// current binary ContextPlayerState protobuf and only bounded selection fields are
// decoded. The virtual position clock remains active while audio is silent.
//
// dev.2l-r2 qualified repeated encrypted media reads over the authenticated AP/Shannon
// channel. The remaining blocker is the media AES key: the current account returns
// correlated AesKeyError 0:1 for every candidate, independently reproduced by current
// librespot. dev.2m performs one bounded candidate scan per manually started session;
// if every candidate is rejected with 0:1 it latches a session-local service-block
// state, suppresses redundant RequestKey scans for later tracks/reconnects, and keeps
// metadata/SPIRC/AP-stream diagnostics alive. No decrypt, decoder or PCM feed is added.
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
  uint8_t credentialAuthType() const { return credentialAuthType_; }
  uint64_t clientSpotifyVersion() const;
  uint8_t clientProductClass() const;
  uint8_t clientPlatformClass() const;
  uint8_t authCpuClass() const;
  uint8_t authOsClass() const;
  const char* authSystemName() const;
  const char* authClientVersion() const;

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
  uint32_t spircPlayPauseFrames() const { return spircPlayPauseFrames_; }
  uint32_t spircSeekFrames() const { return spircSeekFrames_; }
  uint32_t spircPrevFrames() const { return spircPrevFrames_; }
  uint32_t spircNextFrames() const { return spircNextFrames_; }
  uint32_t spircPlaySelectFrames() const { return spircPlaySelectFrames_; }
  uint32_t spircPlaySelectByIndex() const { return spircPlaySelectByIndex_; }
  uint32_t spircReplaceFrames() const { return spircReplaceFrames_; }
  uint32_t spircContextPlayerFrames() const { return spircContextPlayerFrames_; }
  size_t spircContextPlayerBytes() const { return spircContextPlayerBytes_; }
  uint32_t spircContextPlayerPlay() const { return spircContextPlayerPlay_; }
  uint32_t spircContextPlayerSelect() const { return spircContextPlayerSelect_; }
  uint32_t spircContextPlayerByUid() const { return spircContextPlayerByUid_; }
  uint32_t spircContextPlayerByUri() const { return spircContextPlayerByUri_; }
  uint32_t spircContextPlayerByIndex() const { return spircContextPlayerByIndex_; }
  uint32_t spircContextPlayerByScan() const { return spircContextPlayerByScan_; }
  uint32_t spircContextPlayerByInflate() const { return spircContextPlayerByInflate_; }
  uint32_t spircContextPlayerBySkipScan() const { return spircContextPlayerBySkipScan_; }
  uint32_t spircContextPlayerUnresolved() const { return spircContextPlayerUnresolved_; }
  uint32_t spircContextInflateAttempts() const { return spircContextInflateAttempts_; }
  uint32_t spircContextInflateOk() const { return spircContextInflateOk_; }
  uint32_t spircContextInflateFailures() const { return spircContextInflateFailures_; }
  size_t spircContextInflateBytes() const { return spircContextInflateBytes_; }
  uint32_t spircContextInflatePrintablePct() const { return spircContextInflatePrintablePct_; }
  uint32_t spircContextSkipObjects() const { return spircContextSkipObjects_; }
  uint32_t spircContextSkipUidCandidates() const { return spircContextSkipUidCandidates_; }
  uint32_t spircContextSkipUriCandidates() const { return spircContextSkipUriCandidates_; }
  uint32_t spircContextSkipIndexCandidates() const { return spircContextSkipIndexCandidates_; }
  uint32_t spircContextSkipUidResolved() const { return spircContextSkipUidResolved_; }
  uint32_t spircContextSkipQueueResolved() const { return spircContextSkipQueueResolved_; }
  uint32_t spircContextSkipIndexValidated() const { return spircContextSkipIndexValidated_; }
  uint32_t spircContextSkipIndexIgnored() const { return spircContextSkipIndexIgnored_; }
  uint32_t spircContextQueueRefs() const { return spircContextQueueRefs_; }
  uint32_t spircContextQueueGidOnly() const { return spircContextQueueGidOnly_; }
  uint32_t spircContextQueueNativeUri() const { return spircContextQueueNativeUri_; }
  uint32_t spircContextQueueCanonical() const { return spircContextQueueCanonical_; }
  uint32_t spircContextSkipAmbiguous() const { return spircContextSkipAmbiguous_; }
  int32_t spircContextSkipUniqueIndex() const { return spircContextSkipUniqueIndex_; }
  uint32_t spircContextResolveLastUs() const { return spircContextResolveLastUs_; }
  uint32_t spircContextResolveMaxUs() const { return spircContextResolveMaxUs_; }
  uint32_t spircSelectionApplyLastUs() const { return spircSelectionApplyLastUs_; }
  uint32_t spircSelectionApplyMaxUs() const { return spircSelectionApplyMaxUs_; }
  size_t spircContextPlayerLastBytes() const { return spircContextPlayerLastBytes_; }
  uint32_t spircContextPlayerLastHash() const { return spircContextPlayerLastHash_; }
  uint32_t spircContextPlayerPrintablePct() const { return spircContextPlayerPrintablePct_; }
  bool spircContextPlayerProtoValid() const { return spircContextPlayerProtoValid_; }
  uint32_t spircContextPlayerProtoFields() const { return spircContextPlayerProtoFields_; }
  uint32_t spircContextPlayerProtoLengthFields() const { return spircContextPlayerProtoLengthFields_; }
  uint32_t spircContextPlayerQueueMatches() const { return spircContextPlayerQueueMatches_; }
  uint32_t spircContextPlayerNonCurrentMatches() const { return spircContextPlayerNonCurrentMatches_; }
  int32_t spircContextPlayerUniqueIndex() const { return spircContextPlayerUniqueIndex_; }
  uint32_t spircDuplicateLoadsWithUnknownContext() const { return spircDuplicateLoadsWithUnknownContext_; }
  const char* spircContextPlayerEndpoint() const { return spircContextPlayerEndpoint_; }
  const char* spircContextPlayerEncoding() const { return spircContextPlayerEncoding_; }
  const char* spircContextPlayerPrefix() const { return spircContextPlayerPrefix_; }
  const char* spircContextPlayerMagic() const { return spircContextPlayerMagic_; }
  const char* spircContextPlayerProtoMap() const { return spircContextPlayerProtoMap_; }
  const char* spircContextInflateStatus() const { return spircContextInflateStatus_; }
  const char* spircContextInflateEncoding() const { return spircContextInflateEncoding_; }
  uint32_t spircLastType() const { return spircLastType_; }
  bool spircRemoteActive() const { return spircRemoteActive_; }
  bool spircLocalActive() const { return spircLocalActive_; }
  uint32_t spircTransferNotifyAttempts() const { return spircTransferNotifyAttempts_; }
  uint32_t spircTransferNotifySent() const { return spircTransferNotifySent_; }
  uint32_t spircTransferNotifyAcks() const { return spircTransferNotifyAcks_; }
  size_t spircTransferNotifyBytes() const { return spircTransferNotifyBytes_; }
  uint32_t spircBlockedNotifyAttempts() const { return spircBlockedNotifyAttempts_; }
  uint32_t spircBlockedNotifySent() const { return spircBlockedNotifySent_; }
  uint32_t spircBlockedNotifyAcks() const { return spircBlockedNotifyAcks_; }
  size_t spircBlockedNotifyBytes() const { return spircBlockedNotifyBytes_; }
  uint32_t spircEmptyLoadsIgnored() const { return spircEmptyLoadsIgnored_; }
  uint32_t spircDuplicateLoadsIgnored() const { return spircDuplicateLoadsIgnored_; }
  uint32_t spircDuplicateLoadsAcked() const { return spircDuplicateLoadsAcked_; }
  uint32_t spircRecipientIgnored() const { return spircRecipientIgnored_; }
  uint32_t spircCommandAcksSent() const { return spircCommandAcksSent_; }
  uint32_t spircControlNotifySent() const { return spircControlNotifySent_; }
  uint32_t spircControlNotifyAcks() const { return spircControlNotifyAcks_; }
  size_t spircControlNotifyBytes() const { return spircControlNotifyBytes_; }
  size_t spircStateTrackRefCount() const { return spircStateTrackRefs_.size(); }
  size_t spircStateTrackRefBytes() const { return spircStateTrackRefBytes_; }
  uint32_t spircStateTrackRefsTruncated() const { return spircStateTrackRefsTruncated_; }
  uint32_t spircStateFallbackTrackRefs() const { return spircStateFallbackTrackRefs_; }
  uint32_t spircLastLoadTrackCount() const { return spircLastLoadTrackCount_; }
  uint32_t spircLastLoadPositionMs() const { return spircLastLoadPositionMs_; }
  uint32_t spircLastLoadStatus() const { return spircLastLoadStatus_; }
  bool spircPlaybackClockRunning() const { return spircPlaybackClockRunning_; }
  uint32_t spircPlaybackClockBasePositionMs() const { return spircPlaybackClockBasePositionMs_; }
  const char* spircLastLoadContext() const { return spircLastLoadContext_; }
  const char* spircRemoteIdent() const { return spircRemoteIdent_; }
  const char* spircRemoteName() const { return spircRemoteName_; }

  uint32_t trackRefIndex() const { return trackRefIndex_; }
  const char* trackRefGidHex() const { return trackRefGidHex_; }
  const char* trackRefUri() const { return trackRefUri_; }
  // r15 passive media-identity audit. Snapshot views are small and copied under
  // a short critical section; protobuf parsing never runs with that lock held.
  // Manual diagnostics use a separate state machine and never change the normal
  // AudioKey target/latch or expose/retain a received key.
  spotify_key_probe::Reason requestKeyProbe(uint32_t expectedGeneration, const uint8_t expectedGid[16]);
  bool cancelKeyProbe();
  void keyProbeSummary(spotify_key_probe::Summary& out, uint32_t& currentGeneration,
                       bool& transportReady) const;
  bool keyProbeResult(uint32_t run, uint8_t index, spotify_key_probe::Result& out) const;

  void metadataAuditSummary(spotify_metadata_audit::Summary& out) const;
  bool metadataAuditAlternative(uint32_t generation, uint8_t index,
                                spotify_metadata_audit::TrackView& out) const;
  bool metadataAuditKeyTarget(uint32_t generation, spotify_metadata_audit::KeyTarget& out) const;

  uint32_t metadataRequests() const { return metadataRequests_; }
  uint32_t metadataResponses() const { return metadataResponses_; }
  uint32_t metadataSuccesses() const { return metadataSuccesses_; }
  uint32_t metadataParseFailures() const { return metadataParseFailures_; }
  int32_t metadataLastStatus() const { return metadataLastStatus_; }
  size_t metadataLastBytes() const { return metadataLastBytes_; }
  uint32_t metadataLastRoundTripMs() const { return metadataLastRoundTripMs_; }
  uint32_t metadataMaxRoundTripMs() const { return metadataMaxRoundTripMs_; }
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
  const char* mediaKeyStateName() const {
    if (audioKeyBytes_ == 16u) return "ready";
    if (mediaKeyServiceBlocked_) return "service-blocked";
    if (audioKeyPending_) return "requesting";
    if (audioKeyCandidateCount_ != 0u) return "diagnostic";
    return "idle";
  }
  bool mediaKeyServiceBlocked() const { return mediaKeyServiceBlocked_; }
  uint32_t mediaKeyBlockEvents() const { return mediaKeyBlockEvents_; }
  uint32_t mediaKeySuppressedTracks() const { return mediaKeySuppressedTracks_; }
  uint8_t mediaKeyBlockError0() const { return mediaKeyBlockError0_; }
  uint8_t mediaKeyBlockError1() const { return mediaKeyBlockError1_; }

  uint32_t productInfoPackets() const { return productInfoPackets_; }
  size_t productInfoBytes() const { return productInfoBytes_; }
  uint32_t productInfoHash() const { return productInfoHash_; }
  bool productInfoXmlLike() const { return productInfoXmlLike_; }
  const char* productInfoType() const { return productInfoType_; }
  const char* productInfoCatalogue() const { return productInfoCatalogue_; }
  const char* productInfoPlayerLicense() const { return productInfoPlayerLicense_; }
  const char* productInfoHeadFiles() const { return productInfoHeadFiles_; }
  const char* productInfoOnDemand() const { return productInfoOnDemand_; }
  const char* productInfoHighBitrate() const { return productInfoHighBitrate_; }
  const char* productInfoUnrestricted() const { return productInfoUnrestricted_; }
  const char* productInfoMobile() const { return productInfoMobile_; }
  const char* productInfoPrefetchKeys() const { return productInfoPrefetchKeys_; }
  const char* productInfoKeyMemoryCacheMode() const { return productInfoKeyMemoryCacheMode_; }
  const char* productInfoKeyCachingMaxCount() const { return productInfoKeyCachingMaxCount_; }
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
  uint32_t apStreamPostCompletePackets() const { return apStreamPostCompletePackets_; }
  uint32_t apStreamTrackChangeCancels() const { return apStreamTrackChangeCancels_; }
  bool apStreamPending() const { return apStreamPending_; }
  uint16_t apStreamChannelId() const { return apStreamChannelId_; }
  size_t apStreamRequestBytes() const { return apStreamRequestBytes_; }
  uint32_t apStreamRequestedBytes() const { return AP_STREAM_CANARY_BYTES; }
  uint32_t apStreamTotalRequestedBytes() const { return AP_STREAM_CANARY_BYTES * AP_STREAM_PROBE_COUNT; }
  uint8_t apStreamProbeCount() const { return AP_STREAM_PROBE_COUNT; }
  uint8_t apStreamCompletedProbes() const { return apStreamCompletedProbes_; }
  uint32_t apStreamCurrentOffsetBytes() const { return static_cast<uint32_t>(apStreamProbeIndex_) * AP_STREAM_CANARY_BYTES; }
  uint32_t apStreamResponsePackets() const { return apStreamResponsePackets_; }
  uint8_t apStreamLastCommand() const { return apStreamLastCommand_; }
  uint16_t apStreamFailureCode() const { return apStreamFailureCode_; }
  uint32_t apStreamHeaderCount() const { return apStreamHeaderCount_; }
  size_t apStreamHeaderBytes() const { return apStreamHeaderBytes_; }
  uint32_t apStreamReportedFileBytes() const { return apStreamReportedFileBytes_; }
  uint32_t apStreamDataPackets() const { return apStreamDataPackets_; }
  size_t apStreamDataBytes() const { return apStreamDataBytes_; }
  uint32_t apStreamCipherHash() const { return apStreamCipherHash_; }
  size_t apStreamCipherBytesHashed() const { return apStreamCipherBytesHashed_; }
  uint8_t apStreamSourceChunks() const { return apStreamSourceChunks_; }
  uint8_t apStreamSourceChunkMismatches() const { return apStreamSourceChunkMismatches_; }
  bool apStreamSourceContractReady() const {
    return apStreamSourceChunks_ == AP_STREAM_PROBE_COUNT &&
           apStreamSourceChunkMismatches_ == 0u &&
           apStreamCipherBytesHashed_ == AP_STREAM_CANARY_BYTES * AP_STREAM_PROBE_COUNT;
  }
  bool apStreamLiveSourceReady() const { return apStreamMediaSource_.ready(); }
  bool apStreamLiveSourceValid() const { return apStreamMediaSource_.valid(); }
  size_t apStreamLiveSourceRetainedBytes() const { return apStreamMediaSource_.retainedBytes(); }
  size_t apStreamLiveSourceCapacityBytes() const { return apStreamMediaSource_.capacityBytes(); }
  uint32_t apStreamLiveSourceChunks() const { return apStreamMediaSource_.chunksCaptured(); }
  uint32_t apStreamLiveSourceFragments() const { return apStreamMediaSource_.fragmentsCaptured(); }
  uint32_t apStreamLiveSourceFailures() const { return apStreamMediaSource_.captureFailures(); }
  const char* apStreamLiveSourceStorage() const { return apStreamMediaSource_.storageName(); }
  uint32_t apStreamLiveVerifyAttempts() const { return apStreamLiveVerifyAttempts_; }
  uint32_t apStreamLiveVerifySuccesses() const { return apStreamLiveVerifySuccesses_; }
  uint32_t apStreamLiveVerifyFailures() const { return apStreamLiveVerifyFailures_; }
  uint32_t apStreamLiveVerifyReadCalls() const { return apStreamLiveVerifyReadCalls_; }
  uint32_t apStreamLiveVerifyChunks() const { return apStreamLiveVerifyChunks_; }
  size_t apStreamLiveVerifyBytes() const { return apStreamLiveVerifyBytes_; }
  uint32_t apStreamLiveVerifyHash() const { return apStreamLiveVerifyHash_; }
  bool apStreamLiveVerifyHashMatch() const { return apStreamLiveVerifyHashMatch_; }
  bool apStreamLiveVerifyEof() const { return apStreamLiveVerifyEof_; }
  bool apStreamLiveVerifyRewound() const { return apStreamLiveVerifyRewound_; }
  bool apStreamLiveKeyGateEligible() const { return audioKeyBytes_ == 16u && audioKeySuccesses_ != 0u; }
  bool apStreamHeadersComplete() const { return apStreamHeadersComplete_; }
  int32_t apStreamCandidateFormat() const { return apStreamCandidateFormat_; }
  const char* apStreamLastError() const { return apStreamLastError_; }

  const char* apContinuousStateName() const {
    if (!apContinuousAttemptedForTrack_) return "idle";
    if (apContinuousComplete_) return "complete";
    if (apContinuousPending_) return "active";
    return "failed";
  }
  uint32_t apContinuousAttempts() const { return apContinuousAttempts_; }
  uint32_t apContinuousSuccesses() const { return apContinuousSuccesses_; }
  uint32_t apContinuousFailures() const { return apContinuousFailures_; }
  uint32_t apContinuousTimeouts() const { return apContinuousTimeouts_; }
  uint32_t apContinuousProtocolErrors() const { return apContinuousProtocolErrors_; }
  uint32_t apContinuousStalePackets() const { return apContinuousStalePackets_; }
  uint32_t apContinuousPostCompletePackets() const { return apContinuousPostCompletePackets_; }
  uint32_t apContinuousTrackChangeCancels() const { return apContinuousTrackChangeCancels_; }
  bool apContinuousPending() const { return apContinuousPending_; }
  bool apContinuousComplete() const { return apContinuousComplete_; }
  uint16_t apContinuousChannelId() const { return apContinuousChannelId_; }
  uint8_t apContinuousRangeCount() const { return AP_CONTINUOUS_RANGE_COUNT; }
  uint8_t apContinuousCompletedRanges() const { return apContinuousCompletedRanges_; }
  uint32_t apContinuousCurrentOffsetBytes() const {
    return static_cast<uint32_t>(apContinuousRangeIndex_) * AP_CONTINUOUS_RANGE_BYTES;
  }
  size_t apContinuousRangeBytes() const { return AP_CONTINUOUS_RANGE_BYTES; }
  size_t apContinuousTargetBytes() const { return AP_CONTINUOUS_TARGET_BYTES; }
  uint32_t apContinuousResponsePackets() const { return apContinuousResponsePackets_; }
  uint8_t apContinuousLastCommand() const { return apContinuousLastCommand_; }
  uint16_t apContinuousFailureCode() const { return apContinuousFailureCode_; }
  uint32_t apContinuousHeaderCount() const { return apContinuousHeaderCount_; }
  size_t apContinuousHeaderBytes() const { return apContinuousHeaderBytes_; }
  uint32_t apContinuousReportedFileBytes() const { return apContinuousReportedFileBytes_; }
  uint32_t apContinuousDataPackets() const { return apContinuousDataPackets_; }
  size_t apContinuousDataBytes() const { return apContinuousDataBytes_; }
  uint32_t apContinuousProducerHash() const { return apContinuousProducerHash_; }
  uint32_t apContinuousConsumerHash() const { return apContinuousConsumerHash_; }
  size_t apContinuousConsumerBytes() const { return apContinuousConsumerBytes_; }
  uint32_t apContinuousConsumerReads() const { return apContinuousConsumerReads_; }
  bool apContinuousHashMatch() const { return apContinuousHashMatch_; }
  bool apContinuousEof() const { return apContinuousEof_; }
  size_t apContinuousRingCapacityBytes() const { return apContinuousRing_.capacityBytes(); }
  size_t apContinuousRingBufferedBytes() const { return apContinuousRing_.bufferedBytes(); }
  size_t apContinuousRingHighWaterBytes() const { return apContinuousRing_.highWaterBytes(); }
  size_t apContinuousRingProducedBytes() const { return apContinuousRing_.producedBytes(); }
  size_t apContinuousRingConsumedBytes() const { return apContinuousRing_.consumedBytes(); }
  uint32_t apContinuousRingBackpressure() const { return apContinuousRing_.backpressureEvents(); }
  uint32_t apContinuousRingGapErrors() const { return apContinuousRing_.gapErrors(); }
  uint32_t apContinuousRingDuplicateErrors() const { return apContinuousRing_.duplicateErrors(); }
  uint32_t apContinuousRingProducerErrors() const { return apContinuousRing_.producerErrors(); }
  bool apContinuousRingValid() const { return apContinuousRing_.valid(); }
  const char* apContinuousRingStorage() const { return apContinuousRing_.storageName(); }
  int32_t apContinuousCandidateFormat() const { return apContinuousCandidateFormat_; }
  const char* apContinuousLastError() const { return apContinuousLastError_; }

  uint32_t reconnectAttempts() const { return reconnectAttempts_; }
  uint32_t reconnectSuccesses() const { return reconnectSuccesses_; }

  uint32_t lastDurationMs() const;
  uint32_t heapBefore() const { return heapBefore_; }
  uint32_t heapAfter() const { return active() ? ESP.getFreeHeap() : heapAfter_; }
  uint32_t minHeapSeen() const { return ESP.getMinFreeHeap(); }
  UBaseType_t stackMinFree() const { return stackMinFree_; }

private:
  void openKeyProbeSession();
  void closeKeyProbeSession(spotify_key_probe::Reason reason);
  bool takeKeyProbeRequest(spotify_key_probe::Request& out, bool maySend);
  void finishKeyProbeWrite(uint32_t sequence, bool ok);
  bool consumeKeyProbeResponse(uint8_t command, std::vector<uint8_t>& payload);
  // Protected by metadataAuditMux_, so a posted generation and its candidate
  // snapshot are atomic. Lifetime is the boot, not an AP reconnect/reset.
  spotify_key_probe::Probe keyProbe_;
  uint32_t keyProbeSession_ = 0u;
  bool keyProbeReady_ = false;

  void clearMetadataAudit(spotify_metadata_audit::Status status, const uint8_t* requestedGid = nullptr,
                          bool clearStatistics = false);
  void auditMetadataPayload(const std::vector<uint8_t>& payload);
  void recordMetadataKeyTarget(const uint8_t* gid, const uint8_t* file, uint32_t sequence, bool sent);
  void metadataAuditHttpError();
  mutable portMUX_TYPE metadataAuditMux_ = portMUX_INITIALIZER_UNLOCKED;
  spotify_metadata_audit::Report metadataAudit_{};
  spotify_metadata_audit::KeyTarget metadataAuditTarget_{};
  uint32_t metadataAuditGeneration_ = 0u;
  uint32_t metadataAuditAttempts_ = 0u;
  uint32_t metadataAuditFailures_ = 0u;
  uint32_t metadataAuditLastUs_ = 0u;
  uint32_t metadataAuditMaxUs_ = 0u;

  static constexpr uint32_t AUTO_DELAY_MS = 3500u;
  static constexpr uint32_t CONNECT_TIMEOUT_MS = 5000u;
  static constexpr uint32_t IO_TIMEOUT_MS = 7000u;
  static constexpr uint32_t SESSION_POLL_MS = 50u;
  static constexpr uint32_t SESSION_RX_TIMEOUT_MS = 130000u;
  static constexpr uint32_t RECONNECT_DELAY_MS = 2500u;
  static constexpr uint32_t AUDIO_KEY_TIMEOUT_MS = 2500u;
  static constexpr uint8_t MAX_AUDIO_KEY_CANDIDATES = 8u;
  static constexpr size_t MEDIA_HEAD_MAX_BYTES = 4096u;
  static constexpr uint32_t MEDIA_HEAD_TIMEOUT_MS = 5000u;
  static constexpr size_t AP_STREAM_CANARY_BYTES = 4096u;
  static constexpr uint8_t AP_STREAM_PROBE_COUNT = 3u;
  static constexpr uint32_t AP_STREAM_WORD_BYTES = 4u;
  static constexpr uint32_t AP_STREAM_CANARY_WORDS = AP_STREAM_CANARY_BYTES / AP_STREAM_WORD_BYTES;
  static constexpr uint32_t AP_STREAM_TIMEOUT_MS = 5000u;
  static constexpr size_t AP_CONTINUOUS_RANGE_BYTES = 4096u;
  static constexpr uint8_t AP_CONTINUOUS_RANGE_COUNT = 16u;
  static constexpr size_t AP_CONTINUOUS_TARGET_BYTES =
      AP_CONTINUOUS_RANGE_BYTES * AP_CONTINUOUS_RANGE_COUNT;
  static constexpr uint32_t AP_CONTINUOUS_RANGE_WORDS =
      AP_CONTINUOUS_RANGE_BYTES / AP_STREAM_WORD_BYTES;
  static constexpr uint32_t AP_CONTINUOUS_TIMEOUT_MS = 5000u;
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
  void setApContinuousError(const char* text);
  void clearApContinuousTrackState();
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
  uint32_t spircPlayPauseFrames_ = 0u;
  uint32_t spircSeekFrames_ = 0u;
  uint32_t spircPrevFrames_ = 0u;
  uint32_t spircNextFrames_ = 0u;
  uint32_t spircPlaySelectFrames_ = 0u;
  uint32_t spircPlaySelectByIndex_ = 0u;
  uint32_t spircReplaceFrames_ = 0u;
  uint32_t spircContextPlayerFrames_ = 0u;
  size_t spircContextPlayerBytes_ = 0u;
  uint32_t spircContextPlayerPlay_ = 0u;
  uint32_t spircContextPlayerSelect_ = 0u;
  uint32_t spircContextPlayerByUid_ = 0u;
  uint32_t spircContextPlayerByUri_ = 0u;
  uint32_t spircContextPlayerByIndex_ = 0u;
  uint32_t spircContextPlayerByScan_ = 0u;
  uint32_t spircContextPlayerByInflate_ = 0u;
  uint32_t spircContextPlayerBySkipScan_ = 0u;
  uint32_t spircContextPlayerUnresolved_ = 0u;
  uint32_t spircContextInflateAttempts_ = 0u;
  uint32_t spircContextInflateOk_ = 0u;
  uint32_t spircContextInflateFailures_ = 0u;
  size_t spircContextInflateBytes_ = 0u;
  uint32_t spircContextInflatePrintablePct_ = 0u;
  uint32_t spircContextSkipObjects_ = 0u;
  uint32_t spircContextSkipUidCandidates_ = 0u;
  uint32_t spircContextSkipUriCandidates_ = 0u;
  uint32_t spircContextSkipIndexCandidates_ = 0u;
  uint32_t spircContextSkipUidResolved_ = 0u;
  uint32_t spircContextSkipQueueResolved_ = 0u;
  uint32_t spircContextSkipIndexValidated_ = 0u;
  uint32_t spircContextSkipIndexIgnored_ = 0u;
  uint32_t spircContextQueueRefs_ = 0u;
  uint32_t spircContextQueueGidOnly_ = 0u;
  uint32_t spircContextQueueNativeUri_ = 0u;
  uint32_t spircContextQueueCanonical_ = 0u;
  uint32_t spircContextSkipAmbiguous_ = 0u;
  int32_t spircContextSkipUniqueIndex_ = -1;
  uint32_t spircContextResolveLastUs_ = 0u;
  uint32_t spircContextResolveMaxUs_ = 0u;
  uint32_t spircSelectionApplyLastUs_ = 0u;
  uint32_t spircSelectionApplyMaxUs_ = 0u;
  size_t spircContextPlayerLastBytes_ = 0u;
  uint32_t spircContextPlayerLastHash_ = 0u;
  uint32_t spircContextPlayerPrintablePct_ = 0u;
  bool spircContextPlayerProtoValid_ = false;
  uint32_t spircContextPlayerProtoFields_ = 0u;
  uint32_t spircContextPlayerProtoLengthFields_ = 0u;
  uint32_t spircContextPlayerQueueMatches_ = 0u;
  uint32_t spircContextPlayerNonCurrentMatches_ = 0u;
  int32_t spircContextPlayerUniqueIndex_ = -1;
  uint32_t spircDuplicateLoadsWithUnknownContext_ = 0u;
  char spircContextPlayerEndpoint_[24] = {0};
  char spircContextPlayerEncoding_[12] = {0};
  char spircContextPlayerPrefix_[33] = {0};
  char spircContextPlayerMagic_[12] = {0};
  char spircContextPlayerProtoMap_[80] = {0};
  char spircContextInflateStatus_[24] = {0};
  char spircContextInflateEncoding_[12] = {0};
  uint32_t spircLastType_ = 0u;
  bool spircRemoteActive_ = false;
  bool spircLocalActive_ = false;
  uint64_t spircTransferNotifyMercurySequence_ = ~static_cast<uint64_t>(0);
  uint32_t spircTransferNotifyAttempts_ = 0u;
  uint32_t spircTransferNotifySent_ = 0u;
  uint32_t spircTransferNotifyAcks_ = 0u;
  size_t spircTransferNotifyBytes_ = 0u;
  uint64_t spircBlockedNotifyMercurySequence_ = ~static_cast<uint64_t>(0);
  uint32_t spircBlockedNotifyAttempts_ = 0u;
  uint32_t spircBlockedNotifySent_ = 0u;
  uint32_t spircBlockedNotifyAcks_ = 0u;
  size_t spircBlockedNotifyBytes_ = 0u;
  uint32_t spircEmptyLoadsIgnored_ = 0u;
  uint32_t spircDuplicateLoadsIgnored_ = 0u;
  uint32_t spircDuplicateLoadsAcked_ = 0u;
  uint32_t spircRecipientIgnored_ = 0u;
  uint32_t spircCommandAcksSent_ = 0u;
  uint64_t spircControlNotifyMercurySequence_ = ~static_cast<uint64_t>(0);
  uint32_t spircControlNotifySent_ = 0u;
  uint32_t spircControlNotifyAcks_ = 0u;
  size_t spircControlNotifyBytes_ = 0u;
  char spircLastCommandIdent_[48] = {0};
  uint32_t spircLastCommandMsgid_ = 0u;
  bool spircHaveCommandAck_ = false;
  std::vector<std::vector<uint8_t>> spircStateTrackRefs_;
  size_t spircStateTrackRefBytes_ = 0u;
  uint32_t spircStateTrackRefsTruncated_ = 0u;
  uint32_t spircStateFallbackTrackRefs_ = 0u;
  bool spircStateShuffle_ = false;
  bool spircStateHasShuffle_ = false;
  bool spircStateRepeat_ = false;
  bool spircStateHasRepeat_ = false;
  uint32_t spircLastLoadTrackCount_ = 0u;
  uint32_t spircLastLoadPositionMs_ = 0u;
  uint32_t spircLastLoadStatus_ = 0u;
  bool spircPlaybackClockRunning_ = false;
  uint32_t spircPlaybackClockBasePositionMs_ = 0u;
  uint32_t spircPlaybackClockStartedAtMs_ = 0u;
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
  uint32_t metadataRequestedAtMs_ = 0u;
  uint32_t metadataLastRoundTripMs_ = 0u;
  uint32_t metadataMaxRoundTripMs_ = 0u;
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
  bool mediaKeyServiceBlocked_ = false;
  uint32_t mediaKeyBlockEvents_ = 0u;
  uint32_t mediaKeySuppressedTracks_ = 0u;
  uint8_t mediaKeyBlockError0_ = 0u;
  uint8_t mediaKeyBlockError1_ = 0u;

  uint32_t productInfoPackets_ = 0u;
  size_t productInfoBytes_ = 0u;
  uint32_t productInfoHash_ = 0u;
  bool productInfoXmlLike_ = false;
  char productInfoType_[20] = "none";
  char productInfoCatalogue_[20] = "none";
  char productInfoPlayerLicense_[20] = "none";
  char productInfoHeadFiles_[12] = "none";
  char productInfoOnDemand_[12] = "none";
  char productInfoHighBitrate_[12] = "none";
  char productInfoUnrestricted_[12] = "none";
  char productInfoMobile_[12] = "none";
  char productInfoPrefetchKeys_[12] = "none";
  char productInfoKeyMemoryCacheMode_[32] = "none";
  char productInfoKeyCachingMaxCount_[20] = "none";
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
  uint32_t apStreamPostCompletePackets_ = 0u;
  uint32_t apStreamTrackChangeCancels_ = 0u;
  uint16_t apStreamNextChannelId_ = 0u;
  uint16_t apStreamChannelId_ = 0u;
  uint16_t apStreamLastCompletedChannelId_ = 0xffffu;
  uint8_t apStreamProbeIndex_ = 0u;
  uint8_t apStreamCompletedProbes_ = 0u;
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
  size_t apStreamCurrentDataBytes_ = 0u;
  uint32_t apStreamCipherHash_ = 2166136261u;
  size_t apStreamCipherBytesHashed_ = 0u;
  uint8_t apStreamSourceChunks_ = 0u;
  uint8_t apStreamSourceChunkMismatches_ = 0u;
  SpotifyApMediaChunkSource apStreamMediaSource_;
  uint32_t apStreamLiveVerifyAttempts_ = 0u;
  uint32_t apStreamLiveVerifySuccesses_ = 0u;
  uint32_t apStreamLiveVerifyFailures_ = 0u;
  uint32_t apStreamLiveVerifyReadCalls_ = 0u;
  uint32_t apStreamLiveVerifyChunks_ = 0u;
  size_t apStreamLiveVerifyBytes_ = 0u;
  uint32_t apStreamLiveVerifyHash_ = 2166136261u;
  bool apStreamLiveVerifyHashMatch_ = false;
  bool apStreamLiveVerifyEof_ = false;
  bool apStreamLiveVerifyRewound_ = false;
  int32_t apStreamCandidateFormat_ = -1;
  bool apStreamHeadersComplete_ = false;
  bool apStreamPending_ = false;
  bool apStreamAttemptedForTrack_ = false;
  char apStreamLastError_[96] = "none";

  uint32_t apContinuousAttempts_ = 0u;
  uint32_t apContinuousSuccesses_ = 0u;
  uint32_t apContinuousFailures_ = 0u;
  uint32_t apContinuousTimeouts_ = 0u;
  uint32_t apContinuousProtocolErrors_ = 0u;
  uint32_t apContinuousStalePackets_ = 0u;
  uint32_t apContinuousPostCompletePackets_ = 0u;
  uint32_t apContinuousTrackChangeCancels_ = 0u;
  uint16_t apContinuousChannelId_ = 0u;
  uint16_t apContinuousLastCompletedChannelId_ = 0xffffu;
  uint8_t apContinuousTrackGid_[16] = {0};
  uint8_t apContinuousRangeIndex_ = 0u;
  uint8_t apContinuousCompletedRanges_ = 0u;
  uint32_t apContinuousRequestedAtMs_ = 0u;
  size_t apContinuousRequestBytes_ = 0u;
  uint32_t apContinuousResponsePackets_ = 0u;
  uint8_t apContinuousLastCommand_ = 0u;
  uint16_t apContinuousFailureCode_ = 0u;
  uint32_t apContinuousHeaderCount_ = 0u;
  size_t apContinuousHeaderBytes_ = 0u;
  uint32_t apContinuousReportedFileBytes_ = 0u;
  uint32_t apContinuousDataPackets_ = 0u;
  size_t apContinuousDataBytes_ = 0u;
  size_t apContinuousCurrentDataBytes_ = 0u;
  uint32_t apContinuousProducerHash_ = 2166136261u;
  uint32_t apContinuousConsumerHash_ = 2166136261u;
  size_t apContinuousConsumerBytes_ = 0u;
  uint32_t apContinuousConsumerReads_ = 0u;
  bool apContinuousHashMatch_ = false;
  bool apContinuousEof_ = false;
  SpotifyApContinuousRing apContinuousRing_;
  int32_t apContinuousCandidateFormat_ = -1;
  bool apContinuousHeadersComplete_ = false;
  bool apContinuousPending_ = false;
  bool apContinuousAttemptedForTrack_ = false;
  bool apContinuousComplete_ = false;
  char apContinuousLastError_[96] = "none";

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
