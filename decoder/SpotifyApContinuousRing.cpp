#include "SpotifyApContinuousRing.h"

#include <cstring>
#include <cstdlib>

#if __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#define SPOTIFY_AP_CONT_RING_HAVE_HEAP_CAPS 1
#else
#define SPOTIFY_AP_CONT_RING_HAVE_HEAP_CAPS 0
#endif

SpotifyApContinuousRing::~SpotifyApContinuousRing() {
  wipeStorage();
  if (storage_) {
    free(storage_);
    storage_ = nullptr;
  }
}

bool SpotifyApContinuousRing::ensureStorage() {
  if (storage_) return true;
#if SPOTIFY_AP_CONT_RING_HAVE_HEAP_CAPS
  storage_ = static_cast<uint8_t*>(
      heap_caps_malloc(kCapacityBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (storage_) storagePsram_ = true;
#endif
  if (!storage_) {
    storage_ = static_cast<uint8_t*>(malloc(kCapacityBytes));
    storagePsram_ = false;
  }
  if (!storage_) {
    valid_ = false;
    ++producerErrors_;
    return false;
  }
  memset(storage_, 0, kCapacityBytes);
  return true;
}

void SpotifyApContinuousRing::wipeStorage() {
  if (storage_) memset(storage_, 0, kCapacityBytes);
}

bool SpotifyApContinuousRing::begin(uint32_t startOffset, size_t targetBytes) {
  reset();
  if (targetBytes == 0u) {
    valid_ = false;
    ++producerErrors_;
    return false;
  }
  if (!ensureStorage()) return false;
  startOffset_ = startOffset;
  expectedOffset_ = startOffset;
  targetBytes_ = targetBytes;
  active_ = true;
  return true;
}

void SpotifyApContinuousRing::reset() {
  wipeStorage();
  valid_ = true;
  active_ = false;
  producerComplete_ = false;
  startOffset_ = 0u;
  expectedOffset_ = 0u;
  targetBytes_ = 0u;
  producedBytes_ = 0u;
  consumedBytes_ = 0u;
  readPos_ = 0u;
  writePos_ = 0u;
  usedBytes_ = 0u;
  highWaterBytes_ = 0u;
  pushCalls_ = 0u;
  popCalls_ = 0u;
  backpressureEvents_ = 0u;
  gapErrors_ = 0u;
  duplicateErrors_ = 0u;
  producerErrors_ = 0u;
}

void SpotifyApContinuousRing::invalidate() {
  valid_ = false;
  active_ = false;
  producerComplete_ = false;
  usedBytes_ = 0u;
  readPos_ = 0u;
  writePos_ = 0u;
  wipeStorage();
}

bool SpotifyApContinuousRing::push(uint32_t absoluteOffset, const uint8_t* data, size_t len) {
  if (!active_ || !valid_ || producerComplete_ || (!data && len != 0u)) {
    ++producerErrors_;
    valid_ = false;
    return false;
  }
  if (absoluteOffset < expectedOffset_) {
    ++duplicateErrors_;
    ++producerErrors_;
    valid_ = false;
    return false;
  }
  if (absoluteOffset > expectedOffset_) {
    ++gapErrors_;
    ++producerErrors_;
    valid_ = false;
    return false;
  }
  if (len == 0u) return true;
  if (producedBytes_ + len > targetBytes_) {
    ++producerErrors_;
    valid_ = false;
    return false;
  }
  if (len > kCapacityBytes - usedBytes_) {
    ++backpressureEvents_;
    return false;
  }

  size_t first = kCapacityBytes - writePos_;
  if (first > len) first = len;
  memcpy(storage_ + writePos_, data, first);
  if (len > first) memcpy(storage_, data + first, len - first);
  writePos_ = (writePos_ + len) % kCapacityBytes;
  usedBytes_ += len;
  producedBytes_ += len;
  expectedOffset_ += static_cast<uint32_t>(len);
  ++pushCalls_;
  if (usedBytes_ > highWaterBytes_) highWaterBytes_ = usedBytes_;
  return true;
}

bool SpotifyApContinuousRing::finishProducer() {
  if (!active_ || !valid_ || producerComplete_ || producedBytes_ != targetBytes_) {
    ++producerErrors_;
    valid_ = false;
    return false;
  }
  producerComplete_ = true;
  active_ = false;
  return true;
}

bool SpotifyApContinuousRing::pop(uint8_t* dst, size_t capacity, size_t& written) {
  written = 0u;
  if (!dst || capacity == 0u || !valid_) return false;
  if (usedBytes_ == 0u) return true;

  size_t take = usedBytes_;
  if (take > capacity) take = capacity;
  size_t first = kCapacityBytes - readPos_;
  if (first > take) first = take;
  memcpy(dst, storage_ + readPos_, first);
  memset(storage_ + readPos_, 0, first);
  if (take > first) {
    memcpy(dst + first, storage_, take - first);
    memset(storage_, 0, take - first);
  }
  readPos_ = (readPos_ + take) % kCapacityBytes;
  usedBytes_ -= take;
  consumedBytes_ += take;
  written = take;
  ++popCalls_;
  return true;
}

const char* SpotifyApContinuousRing::storageName() const {
  if (!storage_) return "unallocated";
  return storagePsram_ ? "psram" : "internal";
}
