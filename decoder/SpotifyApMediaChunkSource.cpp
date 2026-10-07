#include "SpotifyApMediaChunkSource.h"

#include <Arduino.h>
#include <cstring>
#include <cstdlib>

#if __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#define SPOTIFY_AP_SOURCE_HAVE_HEAP_CAPS 1
#else
#define SPOTIFY_AP_SOURCE_HAVE_HEAP_CAPS 0
#endif

SpotifyApMediaChunkSource::~SpotifyApMediaChunkSource() {
  wipeStorage();
  if (storage_) {
    free(storage_);
    storage_ = nullptr;
  }
}

bool SpotifyApMediaChunkSource::ensureStorage() {
  if (storage_) return true;
#if SPOTIFY_AP_SOURCE_HAVE_HEAP_CAPS
  storage_ = static_cast<uint8_t*>(heap_caps_malloc(kCapacityBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (storage_) storagePsram_ = true;
#endif
  if (!storage_) {
    storage_ = static_cast<uint8_t*>(malloc(kCapacityBytes));
    storagePsram_ = false;
  }
  if (!storage_) {
    valid_ = false;
    ++captureFailures_;
    return false;
  }
  memset(storage_, 0, kCapacityBytes);
  return true;
}

void SpotifyApMediaChunkSource::wipeStorage() {
  if (storage_) memset(storage_, 0, kCapacityBytes);
}

void SpotifyApMediaChunkSource::reset() {
  wipeStorage();
  valid_ = true;
  sealed_ = false;
  writeOffset_ = 0u;
  currentChunkBytes_ = 0u;
  capturedChunks_ = 0u;
  capturedFragments_ = 0u;
  captureFailures_ = 0u;
  readOffset_ = 0u;
  readChunks_ = 0u;
}

bool SpotifyApMediaChunkSource::appendFragment(const uint8_t* data, size_t len) {
  if ((!data && len != 0u) || sealed_ || !valid_) {
    ++captureFailures_;
    valid_ = false;
    return false;
  }
  if (len == 0u) return true;
  if (!ensureStorage()) return false;
  if (capturedChunks_ >= kChunkCount || currentChunkBytes_ + len > kChunkBytes ||
      writeOffset_ + len > kCapacityBytes) {
    ++captureFailures_;
    valid_ = false;
    return false;
  }
  memcpy(storage_ + writeOffset_, data, len);
  writeOffset_ += len;
  currentChunkBytes_ += len;
  ++capturedFragments_;
  return true;
}

bool SpotifyApMediaChunkSource::finishChunk() {
  if (!valid_ || sealed_ || capturedChunks_ >= kChunkCount || currentChunkBytes_ != kChunkBytes) {
    ++captureFailures_;
    valid_ = false;
    return false;
  }
  ++capturedChunks_;
  currentChunkBytes_ = 0u;
  if (capturedChunks_ == kChunkCount) sealed_ = true;
  return true;
}

void SpotifyApMediaChunkSource::invalidate() {
  ++captureFailures_;
  valid_ = false;
  sealed_ = false;
  writeOffset_ = 0u;
  currentChunkBytes_ = 0u;
  capturedChunks_ = 0u;
  capturedFragments_ = 0u;
  readOffset_ = 0u;
  readChunks_ = 0u;
  wipeStorage();
}

bool SpotifyApMediaChunkSource::ready() const {
  return valid_ && sealed_ && capturedChunks_ == kChunkCount &&
         writeOffset_ == kCapacityBytes && currentChunkBytes_ == 0u;
}

bool SpotifyApMediaChunkSource::next(uint8_t* dst, size_t capacity, size_t& written) {
  written = 0u;
  if (!dst || capacity == 0u || !ready()) return false;
  if (readOffset_ >= writeOffset_) return true;

  const size_t offsetInChunk = readOffset_ % kChunkBytes;
  const size_t remainingInChunk = kChunkBytes - offsetInChunk;
  const size_t remainingTotal = writeOffset_ - readOffset_;
  size_t take = remainingInChunk;
  if (remainingTotal < take) take = remainingTotal;
  if (capacity < take) take = capacity;
  if (take == 0u) return false;

  memcpy(dst, storage_ + readOffset_, take);
  readOffset_ += take;
  written = take;
  if ((readOffset_ % kChunkBytes) == 0u) ++readChunks_;
  return true;
}

bool SpotifyApMediaChunkSource::eof() const {
  return ready() && readOffset_ >= writeOffset_;
}

void SpotifyApMediaChunkSource::rewindRead() {
  readOffset_ = 0u;
  readChunks_ = 0u;
}

size_t SpotifyApMediaChunkSource::totalBytes() const {
  return ready() ? writeOffset_ : 0u;
}

const char* SpotifyApMediaChunkSource::storageName() const {
  if (!storage_) return "unallocated";
  return storagePsram_ ? "psram" : "internal";
}
