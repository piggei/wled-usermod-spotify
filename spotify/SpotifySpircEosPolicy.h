#pragma once

#include <stddef.h>
#include <stdint.h>

// Small host-testable policy for dev.2n-r20 virtual end-of-track handling.
// It contains no transport, metadata, decoder or audio side effects; the AP task
// owns those effects after this policy returns a decision.
namespace spotify_spirc_eos {

enum class Action : uint8_t {
  None = 0u,
  Advance = 1u,
  HoldRepeat = 2u,
  HoldBoundary = 3u,
};

struct Input {
  bool localActive;
  bool clockRunning;
  uint32_t playStatus;
  uint32_t positionMs;
  uint32_t durationMs;
  size_t trackIndex;
  size_t trackCount;
  bool repeat;
  uint32_t generation;
  uint32_t handledGeneration;
};

inline Action decide(const Input& in) {
  if (!in.localActive || !in.clockRunning || in.playStatus != 1u) return Action::None;
  if (in.durationMs == 0u || in.generation == 0u || in.generation == in.handledGeneration) {
    return Action::None;
  }
  if (in.positionMs < in.durationMs) return Action::None;
  if (in.repeat) return Action::HoldRepeat;
  if (in.trackCount == 0u || in.trackIndex >= in.trackCount - 1u) return Action::HoldBoundary;
  return Action::Advance;
}

inline const char* actionName(Action action) {
  switch (action) {
    case Action::Advance: return "advance";
    case Action::HoldRepeat: return "repeat-held";
    case Action::HoldBoundary: return "queue-boundary";
    default: return "none";
  }
}

} // namespace spotify_spirc_eos
