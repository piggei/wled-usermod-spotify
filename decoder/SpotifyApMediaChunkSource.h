#pragma once

#include "SpotifyMediaChunkSource.h"

#include <stddef.h>
#include <stdint.h>

// Bounded transient producer for the three qualified AP StreamChunk canary
// ranges. The AP task may fill it from packet fragments, but no decoder owns
// or decrypts/decodes this source in dev.2n-r13. The class intentionally stores only
// encrypted bytes and never accepts key material.
class SpotifyApMediaChunkSource final : public SpotifyMediaChunkSource {
public:
  static constexpr size_t kChunkBytes = 4096u;
  static constexpr uint8_t kChunkCount = 3u;
  static constexpr size_t kCapacityBytes = kChunkBytes * kChunkCount;

  SpotifyApMediaChunkSource() = default;
  ~SpotifyApMediaChunkSource() override;

  SpotifyApMediaChunkSource(const SpotifyApMediaChunkSource&) = delete;
  SpotifyApMediaChunkSource& operator=(const SpotifyApMediaChunkSource&) = delete;

  // SpotifyMediaChunkSource consumer contract. reset() also clears any
  // captured encrypted bytes so a track/session boundary cannot retain media.
  void reset() override;
  bool next(uint8_t* dst, size_t capacity, size_t& written) override;
  bool eof() const override;
  // Rewind only the consumer cursor. Captured encrypted bytes remain sealed
  // and unchanged; reset() remains the destructive wipe operation.
  void rewindRead();
  size_t totalBytes() const override;
  size_t suppliedBytes() const override { return readOffset_; }
  uint32_t chunksSupplied() const override { return readChunks_; }
  const char* name() const override { return "ap-encrypted-canary"; }

  // Producer-side API used only by the AP receive task.
  bool appendFragment(const uint8_t* data, size_t len);
  bool finishChunk();
  void invalidate();

  bool ready() const;
  bool valid() const { return valid_; }
  bool sealed() const { return sealed_; }
  size_t retainedBytes() const { return writeOffset_; }
  size_t capacityBytes() const { return kCapacityBytes; }
  size_t currentChunkBytes() const { return currentChunkBytes_; }
  uint32_t chunksCaptured() const { return capturedChunks_; }
  uint32_t fragmentsCaptured() const { return capturedFragments_; }
  uint32_t captureFailures() const { return captureFailures_; }
  const char* storageName() const;

private:
  bool ensureStorage();
  void wipeStorage();

  uint8_t* storage_ = nullptr;
  bool storagePsram_ = false;
  bool valid_ = true;
  bool sealed_ = false;
  size_t writeOffset_ = 0u;
  size_t currentChunkBytes_ = 0u;
  uint32_t capturedChunks_ = 0u;
  uint32_t capturedFragments_ = 0u;
  uint32_t captureFailures_ = 0u;

  size_t readOffset_ = 0u;
  uint32_t readChunks_ = 0u;
};
