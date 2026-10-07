#include "SpotifyAudioKeyProbe.h"
#include <string.h>

namespace spotify_key_probe {
namespace {
using namespace spotify_metadata_audit;
bool nonzero(const uint8_t* value, size_t bytes) {
  uint8_t any = 0u;
  for (size_t i = 0u; i < bytes; ++i) any |= value[i];
  return any != 0u;
}
Reason makeTarget(const Track& track, int8_t alternative, Target& out) {
  if (!track.hasGid || track.gidConflict || !nonzero(track.gid, kGidBytes))
    return Reason::PrimaryMismatch;
  if (track.territory != Territory::Allowed) return Reason::TerritoryUnverified;
  if (track.filesTruncated || track.invalidFiles || track.filesStored > kMaxFiles)
    return Reason::InvalidFiles;
  const AudioFile* file = nullptr;
  for (uint8_t i = 0u; i < track.filesStored; ++i) {
    if (track.files[i].format != kCompareFormat) continue;
    if (!nonzero(track.files[i].id, kFileIdBytes)) return Reason::InvalidFiles;
    if (file && memcmp(file->id, track.files[i].id, kFileIdBytes) != 0)
      return Reason::InvalidFiles;  // ambiguous same-format files, do not guess
    file = &track.files[i];
  }
  if (!file) return Reason::NoFormat;
  memset(&out, 0, sizeof(out));
  memcpy(out.gid, track.gid, kGidBytes);
  memcpy(out.file, file->id, kFileIdBytes);
  out.format = kCompareFormat;
  out.alternative = alternative;
  out.earliestLive = track.earliestLive;
  return Reason::None;
}
bool samePair(const Target& a, const Target& b) {
  return memcmp(a.gid, b.gid, kGidBytes) == 0 && memcmp(a.file, b.file, kFileIdBytes) == 0;
}
}
Probe::Probe() = default;

Reason Probe::enqueue(const spotify_metadata_audit::Report& metadata, uint32_t generation,
                      uint32_t expectedGeneration, uint32_t session, uint32_t now, bool ready) {
  using namespace spotify_metadata_audit;
  if (busy()) return Reason::Busy;
  if (!generation || generation != expectedGeneration) return Reason::StaleGeneration;
  if (!ready || !session) return Reason::NotReady;
  if (metadata.status != Status::Ok || !metadata.countryValid || !metadata.catalogueValid ||
      metadata.alternativesStored > kMaxAlternatives) return Reason::MetadataNotOk;
  if (!metadata.hasRequestedGid || !metadata.primary.hasGid || metadata.primary.gidConflict ||
      memcmp(metadata.requestedGid, metadata.primary.gid, kGidBytes) != 0)
    return Reason::PrimaryMismatch;
  for (uint8_t i = 0u; i < runsUsed_; ++i) {
    if (memcmp(testedGids_[i], metadata.requestedGid, kGidBytes) == 0) return Reason::AlreadyTested;
  }
  if (runsUsed_ >= kMaxRunsPerBoot || requestsUsed_ >= kMaxRequestsPerBoot) return Reason::BootBudget;
  if (runsUsed_ && static_cast<uint32_t>(now - lastStarted_) < kCooldownMs) return Reason::Cooldown;

  Target targets[kMaxTargets] = {};
  Reason primary = makeTarget(metadata.primary, -1, targets[0]);
  if (primary != Reason::None) return primary;
  uint8_t count = 1u, skipped = 0u, limited = 0u;
  for (uint8_t i = 0u; i < metadata.alternativesStored; ++i) {
    Target next{};
    if (makeTarget(metadata.alternatives[i], static_cast<int8_t>(i), next) != Reason::None) {
      ++skipped;
      continue;
    }
    bool duplicate = false;
    for (uint8_t j = 0u; j < count; ++j) duplicate |= samePair(next, targets[j]);
    if (duplicate) { ++skipped; continue; }
    if (count >= kMaxTargets) { ++limited; continue; }
    targets[count++] = next;
  }
  if (static_cast<unsigned>(requestsUsed_) + count > kMaxRequestsPerBoot) return Reason::BootBudget;

  // Once accepted, this track and one run budget remain consumed even if cancelled.
  memcpy(testedGids_[runsUsed_], metadata.requestedGid, kGidBytes);
  ++runsUsed_;
  run_ = runsUsed_;
  generation_ = generation;
  session_ = session;
  memcpy(gid_, metadata.requestedGid, sizeof(gid_));
  memset(results_, 0, sizeof(results_));
  for (uint8_t i = 0u; i < count; ++i) {
    results_[i].target = targets[i];
    results_[i].outcome = Outcome::Planned;
  }
  count_ = count;
  cursor_ = 0u;
  skipped_ = skipped;
  limited_ = limited;
  started_ = lastStarted_ = now;
  sentAt_ = lastFinished_ = 0u;
  haveFinished_ = false;
  state_ = State::Queued;
  reason_ = Reason::None;
  return Reason::None;
}

bool Probe::pending() const {
  return state_ == State::Running && cursor_ < count_ && results_[cursor_].outcome == Outcome::Pending;
}

bool Probe::takeRequest(uint32_t now, uint32_t generation, uint32_t session,
                       bool ready, uint32_t sequence, Request& out, bool maySend) {
  memset(&out, 0, sizeof(out));
  if (!busy()) return false;
  if (generation != generation_) { cancel(Reason::TrackChanged); return false; }
  if (session != session_) { cancel(Reason::SessionChanged); return false; }
  if (static_cast<uint32_t>(now - started_) >= kRunTimeoutMs) {
    fail(Reason::RunDeadline); return false;
  }
  if (pending()) {
    if (static_cast<uint32_t>(now - sentAt_) < kResponseTimeoutMs) return false;
    results_[cursor_].outcome = Outcome::Timeout;
    results_[cursor_].rttMs = now - sentAt_;
    finishTarget(now);
  }
  if (!busy()) return false;
  if (!ready) { cancel(Reason::NotReady); return false; }
  if (!maySend) return false;  // drain pending AP traffic before sending diagnostics
  if (haveFinished_ && static_cast<uint32_t>(now - lastFinished_) < kRequestGapMs) return false;
  if (cursor_ >= count_ || requestsUsed_ >= kMaxRequestsPerBoot) {
    fail(Reason::BootBudget); return false;
  }
  Result& item = results_[cursor_];
  if (owns(sequence, session_)) { fail(Reason::SequenceConflict); return false; }
  item.sequence = sequence;
  owned_[requestsUsed_].sequence = sequence;
  owned_[requestsUsed_].session = session_;
  ++requestsUsed_;
  item.sent = true;  // attempted send, including a possible partial socket write
  item.outcome = Outcome::Pending;
  sentAt_ = now;
  state_ = State::Running;
  out.target = item.target;
  out.sequence = item.sequence;
  out.run = run_;
  return true;
}

void Probe::finishWrite(uint32_t sequence, bool ok, uint32_t now) {
  if (!pending() || results_[cursor_].sequence != sequence) return;
  if (!ok) {
    results_[cursor_].outcome = Outcome::WriteError;
    results_[cursor_].rttMs = now - sentAt_;
    fail(Reason::WireError);
  }
}

bool Probe::owns(uint32_t sequence, uint32_t session) const {
  for (uint8_t i = 0u; i < requestsUsed_; ++i)
    if (owned_[i].sequence == sequence && owned_[i].session == session) return true;
  return false;
}

bool Probe::consume(uint8_t command, const uint8_t* payload, size_t bytes,
                    uint32_t generation, uint32_t session, uint32_t now) {
  if (command != 0x0Du && command != 0x0Eu) return false;
  if (!payload || bytes < 4u) {
    // There is no normal request in flight when diagnostics are allowed. An
    // uncorrelatable reply aborts this experiment, without touching normal state.
    if (!pending()) return false;
    results_[cursor_].outcome = Outcome::ProtocolError;
    results_[cursor_].command = command;
    results_[cursor_].rttMs = now - sentAt_;
    fail(Reason::MalformedReply);
    return true;
  }
  const uint32_t seq = (static_cast<uint32_t>(payload[0]) << 24u) |
                       (static_cast<uint32_t>(payload[1]) << 16u) |
                       (static_cast<uint32_t>(payload[2]) << 8u) | payload[3];
  if (busy() && (generation != generation_ || session != session_))
    cancel(generation != generation_ ? Reason::TrackChanged : Reason::SessionChanged);
  if (!owns(seq, session)) return false;
  if (!pending() || results_[cursor_].sequence != seq) {
    ++lateResponses_;
    return true;
  }
  // Enforce the same deadline even if the socket is busy (not only on idle polls).
  if (static_cast<uint32_t>(now - sentAt_) >= kResponseTimeoutMs) {
    results_[cursor_].outcome = Outcome::Timeout;
    results_[cursor_].rttMs = now - sentAt_;
    finishTarget(now);
    ++lateResponses_;
    return true;
  }
  Result& item = results_[cursor_];
  item.command = command;
  item.rttMs = now - sentAt_;
  if ((command == 0x0Du && bytes != 20u) || (command == 0x0Eu && bytes != 6u)) {
    item.outcome = Outcome::ProtocolError;
    fail(Reason::MalformedReply);
    return true;
  }
  if (command == 0x0Du) {
    // Structural success only: key bytes are neither read nor copied. The AP
    // adapter securely clears the response vector before returning to the loop.
    item.outcome = Outcome::Accepted;
    item.keyBytes = 16u;
  } else {
    item.outcome = Outcome::Rejected;
    item.error0 = payload[4];
    item.error1 = payload[5];
  }
  finishTarget(now);
  return true;
}

void Probe::finishTarget(uint32_t now) {
  ++cursor_;
  lastFinished_ = now;
  haveFinished_ = true;
  if (cursor_ >= count_) state_ = State::Complete;
}
void Probe::cancel(Reason reason) {
  if (!busy()) return;  // retain completed results across a later track change
  for (uint8_t i = cursor_; i < count_; ++i) {
    if (results_[i].outcome == Outcome::Pending || results_[i].outcome == Outcome::Planned)
      results_[i].outcome = Outcome::Cancelled;
  }
  state_ = State::Cancelled;
  reason_ = reason;
}
void Probe::fail(Reason reason) {
  cancel(reason);
  state_ = State::Failed;
  reason_ = reason;
}

void Probe::summary(uint32_t now, Summary& out) const {
  memset(&out, 0, sizeof(out));
  out.state = state_;
  out.reason = reason_;
  out.run = run_;
  out.generation = generation_;
  out.session = session_;
  memcpy(out.requestedGid, gid_, sizeof(gid_));
  out.targets = count_;
  out.pending = pending();
  out.skippedAlternatives = skipped_;
  out.limitedAlternatives = limited_;
  out.runsUsed = runsUsed_;
  out.requestsUsed = requestsUsed_;
  out.lateResponses = lateResponses_;
  const uint32_t elapsed = now - lastStarted_;
  if (runsUsed_ && elapsed < kCooldownMs) out.cooldownLeftMs = kCooldownMs - elapsed;
  for (uint8_t i = 0u; i < count_; ++i) {
    const Result& r = results_[i];
    if (r.sent) ++out.sent;
    if (r.command) ++out.responses;
    if (r.outcome == Outcome::Accepted) ++out.accepted;
    if (r.outcome == Outcome::Rejected) ++out.rejected;
    if (r.outcome == Outcome::Timeout) ++out.timeouts;
    if (r.outcome == Outcome::ProtocolError) ++out.protocolErrors;
    if (r.outcome == Outcome::WriteError) ++out.writeErrors;
  }
}
bool Probe::result(uint32_t run, uint8_t index, Result& out) const {
  if (!run || run != run_ || index >= count_) return false;
  out = results_[index];
  return true;
}

bool parseGeneration(const char* text, uint32_t& value) {
  value = 0u;
  if (!text || !*text) return false;
  for (size_t i = 0u; text[i]; ++i) {
    if (i >= 10u || text[i] < '0' || text[i] > '9') return false;
    const uint32_t digit = static_cast<uint32_t>(text[i] - '0');
    if (value > (UINT32_MAX - digit) / 10u) return false;
    value = value * 10u + digit;
  }
  return value != 0u;
}
bool parseGid(const char* text, uint8_t out[spotify_metadata_audit::kGidBytes]) {
  if (!text || !out) return false;
  memset(out, 0, spotify_metadata_audit::kGidBytes);
  for (size_t i = 0u; i < 32u; ++i) {
    const char c = text[i];
    uint8_t value = 0u;
    if (c >= '0' && c <= '9') value = static_cast<uint8_t>(c - '0');
    else if (c >= 'a' && c <= 'f') value = static_cast<uint8_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') value = static_cast<uint8_t>(c - 'A' + 10);
    else return false;  // includes early NUL; never reads beyond it
    out[i / 2u] = static_cast<uint8_t>((out[i / 2u] << 4u) | value);
  }
  return text[32] == '\0';
}
const char* stateName(State v) {
  switch (v) {
    case State::Idle: return "idle"; case State::Queued: return "queued";
    case State::Running: return "running"; case State::Complete: return "complete";
    case State::Cancelled: return "cancelled"; case State::Failed: return "failed";
  }
  return "unknown";
}
const char* outcomeName(Outcome v) {
  switch (v) {
    case Outcome::Planned: return "planned"; case Outcome::Pending: return "pending";
    case Outcome::Accepted: return "accepted"; case Outcome::Rejected: return "rejected";
    case Outcome::Timeout: return "timeout"; case Outcome::ProtocolError: return "protocol-error";
    case Outcome::WriteError: return "write-error"; case Outcome::Cancelled: return "cancelled";
  }
  return "unknown";
}
const char* reasonName(Reason v) {
  switch (v) {
    case Reason::None: return "none"; case Reason::Busy: return "busy";
    case Reason::NotReady: return "not-ready"; case Reason::StaleGeneration: return "stale-generation";
    case Reason::StaleTrack: return "stale-track";
    case Reason::MetadataNotOk: return "metadata-not-ok"; case Reason::PrimaryMismatch: return "primary-mismatch";
    case Reason::TerritoryUnverified: return "territory-unverified"; case Reason::InvalidFiles: return "invalid-files";
    case Reason::NoFormat: return "no-format-1"; case Reason::AlreadyTested: return "already-tested-this-boot";
    case Reason::Cooldown: return "cooldown"; case Reason::BootBudget: return "boot-budget-exhausted";
    case Reason::TrackChanged: return "track-changed"; case Reason::SessionChanged: return "session-changed";
    case Reason::SessionClosed: return "session-closed"; case Reason::UserCancel: return "user-cancel";
    case Reason::SessionStop: return "session-stop"; case Reason::RunDeadline: return "run-deadline";
    case Reason::WireError: return "write-failed";
    case Reason::SequenceConflict: return "sequence-conflict"; case Reason::MalformedReply: return "malformed-reply";
  }
  return "unknown";
}
}  // namespace spotify_key_probe
