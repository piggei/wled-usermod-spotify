#pragma once

#include <stddef.h>
#include <stdint.h>
#include "SpotifyMetadataAudit.h"

// Manual, bounded AudioKey comparison. No socket, Arduino, decoder, credentials,
// or key material live here. The AP adapter serializes access under its mux.
namespace spotify_key_probe {
constexpr uint8_t kMaxTargets = 3u;       // primary + at most two alternatives
constexpr uint8_t kMaxRunsPerBoot = 4u;
constexpr uint8_t kMaxRequestsPerBoot = 12u;
constexpr int32_t kCompareFormat = 1;     // OGG_VORBIS_160, identical in every row
constexpr uint32_t kCooldownMs = 10000u;
constexpr uint32_t kRequestGapMs = 500u;
constexpr uint32_t kResponseTimeoutMs = 2500u;
constexpr uint32_t kRunTimeoutMs = 15000u;
// The AP task supplies a sequence from the existing normal allocator. Ownership
// is tracked separately by (AP epoch, sequence), with no new wire value convention.

enum class State : uint8_t { Idle, Queued, Running, Complete, Cancelled, Failed };
enum class Outcome : uint8_t { Planned, Pending, Accepted, Rejected, Timeout,
                               ProtocolError, WriteError, Cancelled };
enum class Reason : uint8_t { None, Busy, NotReady, StaleGeneration, StaleTrack, MetadataNotOk,
    PrimaryMismatch, TerritoryUnverified, InvalidFiles, NoFormat, AlreadyTested,
    Cooldown, BootBudget, TrackChanged, SessionChanged, SessionClosed, UserCancel,
    SessionStop, RunDeadline, WireError, SequenceConflict, MalformedReply };

struct Target {
  uint8_t gid[spotify_metadata_audit::kGidBytes];
  uint8_t file[spotify_metadata_audit::kFileIdBytes];
  int32_t format;
  int8_t alternative;                    // -1 = primary; otherwise metadata index
  bool earliestLive;                     // presence only, never authorization
};
struct Result {
  Target target;
  Outcome outcome;
  uint32_t sequence;
  uint32_t rttMs;
  bool sent;
  uint8_t command;
  uint8_t error0;
  uint8_t error1;
  uint8_t keyBytes;                       // size ONLY; never store a key
};
struct Request {
  Target target;
  uint32_t sequence;
  uint32_t run;
};
struct Summary {
  State state;
  Reason reason;
  uint32_t run;
  uint32_t generation;
  uint32_t session;
  uint8_t requestedGid[spotify_metadata_audit::kGidBytes];
  uint8_t targets;
  uint8_t sent;
  uint8_t responses;
  uint8_t accepted;
  uint8_t rejected;
  uint8_t timeouts;
  uint8_t protocolErrors;
  uint8_t writeErrors;
  uint8_t skippedAlternatives;
  uint8_t limitedAlternatives;
  uint8_t runsUsed;
  uint8_t requestsUsed;
  uint32_t lateResponses;
  bool pending;
  uint32_t cooldownLeftMs;
};

class Probe {
public:
  Probe();
  // Call only with metadata belonging to the selected track and current AP epoch.
  // Accepted requests are queued here; only the AP task can call takeRequest().
  Reason enqueue(const spotify_metadata_audit::Report& metadata, uint32_t generation,
                 uint32_t expectedGeneration, uint32_t session, uint32_t now, bool ready);
  bool takeRequest(uint32_t now, uint32_t generation, uint32_t session,
                   bool ready, uint32_t sequence, Request& out, bool maySend = true);
  void finishWrite(uint32_t sequence, bool ok, uint32_t now);
  // Called only for a successfully authenticated Shannon packet. Returns true
  // for owned replies (including late/duplicate ones), false for normal traffic.
  bool consume(uint8_t command, const uint8_t* payload, size_t bytes,
               uint32_t generation, uint32_t session, uint32_t now);
  void cancel(Reason reason);
  bool busy() const { return state_ == State::Queued || state_ == State::Running; }
  bool pending() const;
  void summary(uint32_t now, Summary& out) const;
  bool result(uint32_t run, uint8_t index, Result& out) const;

private:
  void finishTarget(uint32_t now);
  void fail(Reason reason);
  bool owns(uint32_t sequence, uint32_t session) const;
  State state_ = State::Idle;
  Reason reason_ = Reason::None;
  uint32_t run_ = 0u;
  uint32_t generation_ = 0u;
  uint32_t session_ = 0u;
  uint8_t gid_[spotify_metadata_audit::kGidBytes] = {};
  Result results_[kMaxTargets] = {};
  uint8_t count_ = 0u;
  uint8_t cursor_ = 0u;
  uint8_t skipped_ = 0u;
  uint8_t limited_ = 0u;
  uint32_t started_ = 0u;
  uint32_t sentAt_ = 0u;
  uint32_t lastFinished_ = 0u;
  bool haveFinished_ = false;
  uint32_t lastStarted_ = 0u;
  uint8_t runsUsed_ = 0u;
  uint8_t requestsUsed_ = 0u;
  uint8_t testedGids_[kMaxRunsPerBoot][spotify_metadata_audit::kGidBytes] = {};
  // Keep receipts through cancellation. A reset may restart the normal sequence
  // counter only on a new AP epoch; old receipts must not consume normal replies.
  struct Receipt { uint32_t sequence; uint32_t session; };
  Receipt owned_[kMaxRequestsPerBoot] = {};
  uint32_t lateResponses_ = 0u;
};

const char* stateName(State value);
const char* outcomeName(Outcome value);
const char* reasonName(Reason value);
// Used by the endpoint and covered by the native tests; reject signs, overflow,
// whitespace and partial conversions (String::toInt alone is not sufficient).
bool parseGeneration(const char* text, uint32_t& value);
bool parseGid(const char* text, uint8_t out[spotify_metadata_audit::kGidBytes]);
}  // namespace spotify_key_probe
