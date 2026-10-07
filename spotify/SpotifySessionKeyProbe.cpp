#include "SpotifySessionProbe.h"

spotify_key_probe::Reason SpotifySessionProbe::requestKeyProbe(uint32_t expectedGeneration,
                                                                const uint8_t expectedGid[16]) {
  const uint32_t now = millis();
  portENTER_CRITICAL(&metadataAuditMux_);
  if (!expectedGid || !metadataAudit_.hasRequestedGid ||
      memcmp(expectedGid, metadataAudit_.requestedGid, 16u) != 0) {
    portEXIT_CRITICAL(&metadataAuditMux_);
    return spotify_key_probe::Reason::StaleTrack;
  }
  const auto reason = keyProbe_.enqueue(metadataAudit_, metadataAuditGeneration_,
                                       expectedGeneration, keyProbeSession_, now, keyProbeReady_);
  portEXIT_CRITICAL(&metadataAuditMux_);
  return reason;
}

bool SpotifySessionProbe::cancelKeyProbe() {
  portENTER_CRITICAL(&metadataAuditMux_);
  const bool active = keyProbe_.busy();
  keyProbe_.cancel(spotify_key_probe::Reason::UserCancel);
  portEXIT_CRITICAL(&metadataAuditMux_);
  return active;
}

void SpotifySessionProbe::keyProbeSummary(spotify_key_probe::Summary& out,
                                          uint32_t& currentGeneration, bool& transportReady) const {
  const uint32_t now = millis();
  portENTER_CRITICAL(&metadataAuditMux_);
  keyProbe_.summary(now, out);
  currentGeneration = metadataAuditGeneration_;
  transportReady = keyProbeReady_;
  portEXIT_CRITICAL(&metadataAuditMux_);
}

bool SpotifySessionProbe::keyProbeResult(uint32_t run, uint8_t index,
                                         spotify_key_probe::Result& out) const {
  portENTER_CRITICAL(&metadataAuditMux_);
  const bool valid = keyProbe_.result(run, index, out);
  portEXIT_CRITICAL(&metadataAuditMux_);
  return valid;
}

void SpotifySessionProbe::openKeyProbeSession() {
  portENTER_CRITICAL(&metadataAuditMux_);
  keyProbe_.cancel(spotify_key_probe::Reason::SessionChanged);
  keyProbeReady_ = false;
  if (++keyProbeSession_ == 0u) ++keyProbeSession_;
  portEXIT_CRITICAL(&metadataAuditMux_);
}

void SpotifySessionProbe::closeKeyProbeSession(spotify_key_probe::Reason reason) {
  portENTER_CRITICAL(&metadataAuditMux_);
  keyProbeReady_ = false;
  keyProbe_.cancel(reason);
  portEXIT_CRITICAL(&metadataAuditMux_);
}

bool SpotifySessionProbe::takeKeyProbeRequest(spotify_key_probe::Request& out, bool maySend) {
  // Called on the AP task only. Do not overlap normal key/metadata/canary work.
  // The primary/alternatives are already from the same audited metadata body.
  const bool transportReady = !stopRequested_ && authenticated() &&
      !audioKeyPending_ && metadataMercurySequence_ == ~static_cast<uint64_t>(0) &&
      !apStreamPending_ && apStreamCompletedProbes_ == AP_STREAM_PROBE_COUNT &&
      apStreamLiveVerifySuccesses_ != 0u &&
      audioKeyNextSequence_ != UINT32_MAX;
  const uint32_t now = millis();
  portENTER_CRITICAL(&metadataAuditMux_);
  keyProbeReady_ = transportReady && metadataAudit_.status == spotify_metadata_audit::Status::Ok;
  const bool send = keyProbe_.takeRequest(now, metadataAuditGeneration_, keyProbeSession_,
                                         keyProbeReady_, audioKeyNextSequence_, out, maySend);
  if (send) ++audioKeyNextSequence_;  // same allocator as normal RequestKey, AP task only
  portEXIT_CRITICAL(&metadataAuditMux_);
  return send;
}

void SpotifySessionProbe::finishKeyProbeWrite(uint32_t sequence, bool ok) {
  const uint32_t now = millis();
  portENTER_CRITICAL(&metadataAuditMux_);
  keyProbe_.finishWrite(sequence, ok, now);
  portEXIT_CRITICAL(&metadataAuditMux_);
}

bool SpotifySessionProbe::consumeKeyProbeResponse(uint8_t command, std::vector<uint8_t>& payload) {
  if (command != 0x0Du && command != 0x0Eu) return false;
  const uint32_t now = millis();
  portENTER_CRITICAL(&metadataAuditMux_);
  const bool handled = keyProbe_.consume(command, payload.data(), payload.size(),
                                        metadataAuditGeneration_, keyProbeSession_, now);
  portEXIT_CRITICAL(&metadataAuditMux_);
  if (handled) {
    // No diagnostic key is copied into audioKey_, persisted, logged or decoded.
    // Volatile stores prevent the optimizer from eliding the response wipe.
    volatile uint8_t* bytes = payload.data();
    for (size_t i = 0u; i < payload.size(); ++i) bytes[i] = 0u;
  }
  return handled;
}
