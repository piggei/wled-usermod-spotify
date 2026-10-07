#pragma once

#include <stddef.h>
#include <stdint.h>

// Bounded, diagnostic-only view of spotify.metadata.Track. No Arduino, network,
// allocation, playback state or credentials live in this parser. Catalogue / country
// are supplied by the authenticated session, never inferred from the host location.
namespace spotify_metadata_audit {
constexpr size_t kGidBytes = 16u;
constexpr size_t kFileIdBytes = 20u;
constexpr size_t kMaxAlternatives = 8u;
constexpr size_t kMaxFiles = 8u;
constexpr size_t kMaxMetadataBytes = 65536u;
constexpr size_t kMaxFields = 4096u;
constexpr size_t kMaxCountryListBytes = 1024u;
constexpr size_t kCatalogueBytes = 24u;

enum class Status : uint8_t { Idle, Pending, Ok, Limited, Malformed, TooLarge, Budget, NoMemory, HttpError };
enum class Territory : uint8_t { Unknown, Allowed, Restricted };
enum class Pair : uint8_t { Unknown, Match, Mismatch };

struct AudioFile {
  uint8_t id[kFileIdBytes];
  int32_t format;
};
struct Track {
  uint8_t gid[kGidBytes];
  bool hasGid;
  bool gidConflict;
  AudioFile files[kMaxFiles];
  uint16_t filesSeen;
  uint8_t filesStored;
  uint16_t invalidFiles;
  bool filesTruncated;
  uint16_t restrictions;
  uint16_t applicable;
  uint16_t allowedRules;
  uint16_t deniedRules;
  uint16_t unknownRules;
  uint16_t ignoredRules;
  uint16_t allowLists;
  uint16_t denyLists;
  uint16_t availability;
  uint16_t salePeriods;
  uint16_t nestedAlternatives;
  bool earliestLive;
  Territory territory;
};
struct Report {
  Status status;
  char country[3];
  char catalogue[kCatalogueBytes];
  bool countryValid;
  bool catalogueValid;
  uint8_t requestedGid[kGidBytes];
  bool hasRequestedGid;
  Track primary;
  Track alternatives[kMaxAlternatives];
  uint16_t alternativesSeen;
  uint8_t alternativesStored;
  bool alternativesTruncated;
  size_t inputBytes;
  size_t fieldsRead;
};

// A small stack-safe view for JSON telemetry; do not copy Report on the async
// web-server task stack. "Allowed" means the parsed country rule permits the
// market, NOT that the keymaster has authorized playback.
struct TrackView {
  uint8_t gid[kGidBytes];
  bool hasGid;
  bool gidConflict;
  uint16_t filesSeen;
  uint8_t filesStored;
  uint16_t invalidFiles;
  bool filesTruncated;
  int32_t preferredFormat;
  uint8_t preferredFile[kFileIdBytes];
  bool hasPreferredFile;
  uint16_t restrictions;
  uint16_t applicable;
  uint16_t unknownRules;
  uint16_t ignoredRules;
  uint16_t allowLists;
  uint16_t denyLists;
  uint16_t availability;
  uint16_t salePeriods;
  uint16_t nestedAlternatives;
  bool earliestLive;
  Territory territory;
};
struct Summary {
  Status status;
  uint32_t generation;
  uint32_t attempts;
  uint32_t failures;
  uint32_t lastUs;
  uint32_t maxUs;
  char country[3];
  char catalogue[kCatalogueBytes];
  uint8_t requestedGid[kGidBytes];
  bool hasRequestedGid;
  TrackView primary;
  uint16_t alternativesSeen;
  uint8_t alternativesStored;
  bool alternativesTruncated;
  size_t inputBytes;
  size_t fieldsRead;
};
struct KeyTarget {
  uint8_t gid[kGidBytes];
  uint8_t file[kFileIdBytes];
  Pair pair;
  bool present;
  bool sent;
  uint32_t sequence;
};

void reset(Report& out);
bool parse(const uint8_t* data, size_t bytes, const uint8_t* requestedGid,
           const char* country, const char* catalogue, Report& out);
void view(const Track& track, TrackView& out);
Pair comparePrimaryPair(const Report& report, const uint8_t* gid, const uint8_t* file);
const char* statusName(Status value);
const char* territoryName(Territory value);
const char* pairName(Pair value);
void hexId(const uint8_t* bytes, size_t length, char* out, size_t capacity);
}  // namespace spotify_metadata_audit
