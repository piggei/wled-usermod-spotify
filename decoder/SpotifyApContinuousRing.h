#pragma once

#include <stddef.h>
#include <stdint.h>

// dev.2n-r17: bounded encrypted-byte transport buffer for the live AP diagnostic
// path. This object never accepts media keys and never decrypts or decodes data.
// A producer appends strictly monotonic absolute file offsets while a diagnostic
// consumer drains bytes independently. Storage is capped at 64 KiB and prefers
// PSRAM on ESP32 targets.
class SpotifyApContinuousRing {
public:
  static constexpr size_t kCapacityBytes = 64u * 1024u;

  SpotifyApContinuousRing() = default;
  ~SpotifyApContinuousRing();

  SpotifyApContinuousRing(const SpotifyApContinuousRing&) = delete;
  SpotifyApContinuousRing& operator=(const SpotifyApContinuousRing&) = delete;

  bool begin(uint32_t startOffset, size_t targetBytes);
  void reset();
  void invalidate();

  // Producer API. absoluteOffset must exactly match the next expected byte.
  // Offset gaps/duplicates are rejected and invalidate the current transfer.
  // Capacity pressure is reported separately and does not silently overwrite.
  bool push(uint32_t absoluteOffset, const uint8_t* data, size_t len);
  bool finishProducer();

  // Consumer API. Empty-but-not-finished is not EOF: pop succeeds with
  // written=0, allowing a future decoder task to wait for more producer data.
  bool pop(uint8_t* dst, size_t capacity, size_t& written);

  bool valid() const { return valid_; }
  bool active() const { return active_; }
  bool producerComplete() const { return producerComplete_; }
  bool eof() const { return producerComplete_ && usedBytes_ == 0u; }
  size_t capacityBytes() const { return kCapacityBytes; }
  size_t targetBytes() const { return targetBytes_; }
  size_t bufferedBytes() const { return usedBytes_; }
  size_t highWaterBytes() const { return highWaterBytes_; }
  size_t producedBytes() const { return producedBytes_; }
  size_t consumedBytes() const { return consumedBytes_; }
  uint32_t expectedOffset() const { return expectedOffset_; }
  uint32_t startOffset() const { return startOffset_; }
  uint32_t pushCalls() const { return pushCalls_; }
  uint32_t popCalls() const { return popCalls_; }
  uint32_t backpressureEvents() const { return backpressureEvents_; }
  uint32_t gapErrors() const { return gapErrors_; }
  uint32_t duplicateErrors() const { return duplicateErrors_; }
  uint32_t producerErrors() const { return producerErrors_; }
  uint32_t writeWraps() const { return writeWraps_; }
  uint32_t readWraps() const { return readWraps_; }
  const char* storageName() const;

private:
  bool ensureStorage();
  void wipeStorage();

  uint8_t* storage_ = nullptr;
  bool storagePsram_ = false;
  bool valid_ = true;
  bool active_ = false;
  bool producerComplete_ = false;
  uint32_t startOffset_ = 0u;
  uint32_t expectedOffset_ = 0u;
  size_t targetBytes_ = 0u;
  size_t producedBytes_ = 0u;
  size_t consumedBytes_ = 0u;
  size_t readPos_ = 0u;
  size_t writePos_ = 0u;
  size_t usedBytes_ = 0u;
  size_t highWaterBytes_ = 0u;
  uint32_t pushCalls_ = 0u;
  uint32_t popCalls_ = 0u;
  uint32_t backpressureEvents_ = 0u;
  uint32_t gapErrors_ = 0u;
  uint32_t duplicateErrors_ = 0u;
  uint32_t producerErrors_ = 0u;
  uint32_t writeWraps_ = 0u;
  uint32_t readWraps_ = 0u;
};
