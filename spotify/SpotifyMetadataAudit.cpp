#include "SpotifyMetadataAudit.h"

#include <limits.h>
#include <string.h>

namespace spotify_metadata_audit {
namespace {
struct Slice { const uint8_t* data; size_t size; };
struct Field { uint32_t number; uint8_t wire; uint64_t scalar; Slice bytes; };
struct Context { Report& report; bool malformed; bool exhausted; };

bool varint(Slice in, size_t& pos, uint64_t& value) {
  value = 0u;
  for (unsigned int i = 0u; i < 10u; ++i) {
    if (pos >= in.size) return false;
    const uint8_t byte = in.data[pos++];
    if (i == 9u && byte > 1u) return false;
    value |= static_cast<uint64_t>(byte & 0x7fu) << (7u * i);
    if ((byte & 0x80u) == 0u) return true;
  }
  return false;
}
bool next(Slice in, size_t& pos, Field& out, Context& ctx) {
  if (++ctx.report.fieldsRead > kMaxFields) { ctx.exhausted = true; return false; }
  uint64_t tag = 0u;
  if (!varint(in, pos, tag) || (tag >> 3u) == 0u || (tag >> 3u) > 0x1fffffffu) {
    ctx.malformed = true; return false;
  }
  out = Field{};
  out.number = static_cast<uint32_t>(tag >> 3u);
  out.wire = static_cast<uint8_t>(tag & 7u);
  uint64_t length = 0u;
  switch (out.wire) {
    case 0u:
      if (!varint(in, pos, out.scalar)) { ctx.malformed = true; return false; }
      return true;
    case 1u: length = 8u; break;
    case 2u:
      if (!varint(in, pos, length)) { ctx.malformed = true; return false; }
      break;
    case 5u: length = 4u; break;
    default: ctx.malformed = true; return false;
  }
  if (length > in.size - pos) { ctx.malformed = true; return false; }
  out.bytes = Slice{in.data + pos, static_cast<size_t>(length)};
  pos += static_cast<size_t>(length);
  return true;
}
size_t boundedLength(const char* text, size_t limit) {
  size_t length = 0u;
  while (length < limit && text[length] != '\0') ++length;
  return length;
}
bool uppercase(uint8_t c) { return c >= 'A' && c <= 'Z'; }
bool stringIs(Slice bytes, const char* text) {
  const size_t len = strlen(text);
  return bytes.size == len && memcmp(bytes.data, text, len) == 0;
}
bool countryList(Slice bytes, const Report& report, bool& contains) {
  contains = false;
  if (!report.countryValid || bytes.size > kMaxCountryListBytes || (bytes.size & 1u)) return false;
  for (size_t i = 0u; i < bytes.size; i += 2u) {
    if (!uppercase(bytes.data[i]) || !uppercase(bytes.data[i + 1u])) return false;
    if (bytes.data[i] == static_cast<uint8_t>(report.country[0]) &&
        bytes.data[i + 1u] == static_cast<uint8_t>(report.country[1])) contains = true;
  }
  return true;
}
struct Selectors { bool present; bool match; bool unknown; };
void legacyCatalogue(uint64_t value, const Report& report, Selectors& selectors) {
  selectors.present = true;
  // Do not guess how a new/custom catalogue maps to legacy numeric enums.
  if (!report.catalogueValid || strcmp(report.catalogue, "premium") != 0) {
    selectors.unknown = true; return;
  }
  if (value == 1u || value == 2u) selectors.match = true; // SUBSCRIPTION / ALL
  else if (value > 4u) selectors.unknown = true;
}
bool restriction(Slice bytes, Track& track, Context& ctx) {
  ++track.restrictions;
  Selectors numeric{}, modern{};
  bool unsupported = false;
  bool seenType = false;
  uint64_t type = 0u;
  bool hasAllowed = false, hasForbidden = false;
  bool allowedContains = false, forbiddenContains = false;
  size_t pos = 0u;
  while (pos < bytes.size) {
    Field field{};
    if (!next(bytes, pos, field, ctx)) return false;
    if (field.number == 1u) {
      if (field.wire == 0u) legacyCatalogue(field.scalar, ctx.report, numeric);
      else if (field.wire == 2u) {
        size_t packedPos = 0u;
        while (packedPos < field.bytes.size) {
          if (++ctx.report.fieldsRead > kMaxFields) { ctx.exhausted = true; return false; }
          uint64_t value = 0u;
          if (!varint(field.bytes, packedPos, value)) { ctx.malformed = true; return false; }
          legacyCatalogue(value, ctx.report, numeric);
        }
      } else unsupported = true;
    } else if (field.number == 5u) {
      modern.present = true;
      if (field.wire != 2u || field.bytes.size == 0u || field.bytes.size >= kCatalogueBytes ||
          !ctx.report.catalogueValid) modern.unknown = true;
      else if (stringIs(field.bytes, ctx.report.catalogue)) modern.match = true;
    } else if (field.number == 4u) {
      if (field.wire != 0u || (seenType && field.scalar != type) || field.scalar != 0u) unsupported = true;
      type = field.scalar;
      seenType = true;
    } else if (field.number == 2u || field.number == 3u) {
      const bool allowed = field.number == 2u;
      bool contains = false;
      if (field.wire != 2u || !countryList(field.bytes, ctx.report, contains)) unsupported = true;
      if (allowed) {
        ++track.allowLists;
        if (hasAllowed) unsupported = true;
        hasAllowed = true; allowedContains = contains;
      } else {
        ++track.denyLists;
        if (hasForbidden) unsupported = true;
        hasForbidden = true; forbiddenContains = contains;
      }
    } else unsupported = true; // unknown restriction semantics are not authorization
  }
  // Modern catalogue_str is authoritative when present. If BOTH selector forms
  // are explicit and disagree, retain unknown rather than silently picking one.
  const Selectors& chosen = modern.present ? modern : numeric;
  const bool conflict = modern.present && numeric.present &&
      !modern.unknown && !numeric.unknown && modern.match != numeric.match;
  if (!chosen.present || chosen.unknown || conflict) {
    ++track.unknownRules; return true;
  }
  if (!chosen.match) { ++track.ignoredRules; return true; }
  ++track.applicable;
  if (unsupported || hasAllowed == hasForbidden) { ++track.unknownRules; return true; }
  const bool permits = hasAllowed ? allowedContains : !forbiddenContains;
  if (permits) ++track.allowedRules;
  else ++track.deniedRules;
  return true;
}
bool audioFile(Slice bytes, Track& track, Context& ctx) {
  ++track.filesSeen;
  AudioFile file{};
  file.format = -1;
  bool hasId = false, invalid = false, hasFormat = false;
  size_t pos = 0u;
  while (pos < bytes.size) {
    Field field{};
    if (!next(bytes, pos, field, ctx)) return false;
    if (field.number == 1u) {
      if (field.wire != 2u || field.bytes.size != kFileIdBytes) invalid = true;
      else {
        if (hasId && memcmp(file.id, field.bytes.data, kFileIdBytes) != 0) invalid = true;
        memcpy(file.id, field.bytes.data, kFileIdBytes); hasId = true;
      }
    } else if (field.number == 2u) {
      if (field.wire != 0u || field.scalar > INT32_MAX) invalid = true;
      else {
        const int32_t format = static_cast<int32_t>(field.scalar);
        if (hasFormat && format != file.format) invalid = true;
        file.format = format; hasFormat = true;
      }
    }
  }
  if (!hasId || invalid) { ++track.invalidFiles; return true; }
  // Preserve metadata order, including duplicate messages, within a fixed bound.
  if (track.filesStored < kMaxFiles) track.files[track.filesStored++] = file;
  else track.filesTruncated = true;
  return true;
}
void finishTerritory(Track& track) {
  track.territory = Territory::Unknown;
  if (track.unknownRules || (track.allowedRules && track.deniedRules)) return;
  if (track.deniedRules) track.territory = Territory::Restricted;
  else if (track.allowedRules) track.territory = Territory::Allowed;
}
bool trackMessage(Slice bytes, Track& track, Context& ctx, bool isPrimary) {
  size_t pos = 0u;
  while (pos < bytes.size) {
    Field field{};
    if (!next(bytes, pos, field, ctx)) return false;
    if (field.number == 1u) {
      if (field.wire != 2u || field.bytes.size != kGidBytes) { ctx.malformed = true; return false; }
      if (track.hasGid && memcmp(track.gid, field.bytes.data, kGidBytes) != 0) track.gidConflict = true;
      memcpy(track.gid, field.bytes.data, kGidBytes); track.hasGid = true;
    } else if (field.number == 11u) {
      if (field.wire != 2u) { ctx.malformed = true; return false; }
      if (!restriction(field.bytes, track, ctx)) return false;
    } else if (field.number == 12u) {
      if (field.wire != 2u) { ctx.malformed = true; return false; }
      if (!audioFile(field.bytes, track, ctx)) return false;
    } else if (field.number == 13u) {
      if (field.wire != 2u) { ctx.malformed = true; return false; }
      if (!isPrimary) { ++track.nestedAlternatives; continue; } // no recursion beyond depth 1
      ++ctx.report.alternativesSeen;
      if (ctx.report.alternativesStored == kMaxAlternatives) {
        ctx.report.alternativesTruncated = true; continue;
      }
      Track& alternative = ctx.report.alternatives[ctx.report.alternativesStored++];
      if (!trackMessage(field.bytes, alternative, ctx, false)) return false;
    } else if (field.number == 14u || field.number == 19u) {
      if (field.wire != 2u) { ctx.malformed = true; return false; }
      if (field.number == 14u) ++track.salePeriods;
      else ++track.availability;
    } else if (field.number == 17u) {
      if (field.wire != 0u) { ctx.malformed = true; return false; }
      track.earliestLive = true;
    }
  }
  finishTerritory(track);
  return true;
}
bool limitedTrack(const Track& track) {
  return track.filesTruncated || track.invalidFiles || track.gidConflict || track.nestedAlternatives;
}
} // namespace

void reset(Report& out) {
  // POD, fixed size. No whole Report temporary on the ESP32 AP stack.
  memset(&out, 0, sizeof(out));
}
bool parse(const uint8_t* data, size_t bytes, const uint8_t* requestedGid,
           const char* country, const char* catalogue, Report& out) {
  reset(out);
  out.inputBytes = bytes;
  if (requestedGid) { memcpy(out.requestedGid, requestedGid, kGidBytes); out.hasRequestedGid = true; }
  if (country && boundedLength(country, 3u) == 2u && uppercase(static_cast<uint8_t>(country[0])) &&
      uppercase(static_cast<uint8_t>(country[1]))) {
    memcpy(out.country, country, 2u); out.countryValid = true;
  }
  if (catalogue) {
    const size_t len = boundedLength(catalogue, kCatalogueBytes);
    if (len > 0u && len < kCatalogueBytes && strcmp(catalogue, "none") != 0 && strcmp(catalogue, "missing") != 0) {
      memcpy(out.catalogue, catalogue, len); out.catalogueValid = true;
    }
  }
  if (bytes > kMaxMetadataBytes) { out.status = Status::TooLarge; return false; }
  if (!data || !bytes) { out.status = Status::Malformed; return false; }
  Context context{out, false, false};
  if (!trackMessage(Slice{data, bytes}, out.primary, context, true)) {
    out.status = context.exhausted ? Status::Budget : Status::Malformed;
    // Partial parses must never leave a visible affirmative territory result.
    out.primary.territory = Territory::Unknown;
    for (size_t i = 0u; i < out.alternativesStored; ++i) out.alternatives[i].territory = Territory::Unknown;
    return false;
  }
  bool limited = out.alternativesTruncated || limitedTrack(out.primary);
  for (size_t i = 0u; i < out.alternativesStored; ++i) limited |= limitedTrack(out.alternatives[i]);
  out.status = limited ? Status::Limited : Status::Ok;
  return true;
}
void view(const Track& track, TrackView& out) {
  memset(&out, 0, sizeof(out));
  memcpy(out.gid, track.gid, kGidBytes);
  out.hasGid = track.hasGid;
  out.gidConflict = track.gidConflict;
  out.filesSeen = track.filesSeen;
  out.filesStored = track.filesStored;
  out.invalidFiles = track.invalidFiles;
  out.filesTruncated = track.filesTruncated;
  out.preferredFormat = -1;
  if (track.filesStored) {
    size_t selected = 0u;
    for (size_t i = 0u; i < track.filesStored; ++i) {
      if (track.files[i].format == 1) { selected = i; break; }
    }
    out.hasPreferredFile = true;
    out.preferredFormat = track.files[selected].format;
    memcpy(out.preferredFile, track.files[selected].id, kFileIdBytes);
  }
  out.restrictions = track.restrictions;
  out.applicable = track.applicable;
  out.unknownRules = track.unknownRules;
  out.ignoredRules = track.ignoredRules;
  out.allowLists = track.allowLists;
  out.denyLists = track.denyLists;
  out.availability = track.availability;
  out.salePeriods = track.salePeriods;
  out.nestedAlternatives = track.nestedAlternatives;
  out.earliestLive = track.earliestLive;
  out.territory = track.territory;
}
Pair comparePrimaryPair(const Report& report, const uint8_t* gid, const uint8_t* file) {
  if (!gid || !file || (report.status != Status::Ok && report.status != Status::Limited)) return Pair::Unknown;
  const Track& primary = report.primary;
  if (!primary.hasGid || primary.gidConflict) return Pair::Unknown;
  if (memcmp(gid, primary.gid, kGidBytes) != 0) return Pair::Mismatch;
  for (size_t i = 0u; i < primary.filesStored; ++i) {
    if (memcmp(file, primary.files[i].id, kFileIdBytes) == 0) return Pair::Match;
  }
  return primary.filesTruncated || primary.invalidFiles ? Pair::Unknown : Pair::Mismatch;
}
const char* statusName(Status value) {
  switch (value) {
    case Status::Idle: return "idle"; case Status::Pending: return "pending";
    case Status::Ok: return "ok"; case Status::Limited: return "limited";
    case Status::Malformed: return "malformed"; case Status::TooLarge: return "too-large";
    case Status::Budget: return "field-budget"; case Status::NoMemory: return "no-memory";
    case Status::HttpError: return "metadata-http-error";
  }
  return "unknown";
}
const char* territoryName(Territory value) {
  switch (value) {
    case Territory::Allowed: return "allowed";
    case Territory::Restricted: return "restricted";
    default: return "unknown";
  }
}
const char* pairName(Pair value) {
  switch (value) { case Pair::Match: return "match"; case Pair::Mismatch: return "mismatch"; default: return "unknown"; }
}
void hexId(const uint8_t* bytes, size_t length, char* out, size_t capacity) {
  if (!out || !capacity) return;
  out[0] = '\0';
  if (!bytes || length > (capacity - 1u) / 2u) return;
  static const char kHexDigits[] = "0123456789abcdef"; // macro-safe on Arduino
  for (size_t i = 0u; i < length; ++i) {
    out[2u * i] = kHexDigits[bytes[i] >> 4u];
    out[2u * i + 1u] = kHexDigits[bytes[i] & 15u];
  }
  out[length * 2u] = '\0';
}
} // namespace spotify_metadata_audit
