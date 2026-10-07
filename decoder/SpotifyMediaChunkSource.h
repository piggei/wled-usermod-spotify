#pragma once
#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <cstring>

// Bounded producer contract for media bytes delivered in transport-sized
// chunks. The decoder owns staging and optional decrypt state; a source only
// supplies opaque bytes and never exposes credentials or key material.
class SpotifyMediaChunkSource {
public:
  virtual ~SpotifyMediaChunkSource() = default;

  virtual void reset() = 0;
  virtual bool next(uint8_t* dst, size_t capacity, size_t& written) = 0;
  virtual bool eof() const = 0;
  virtual size_t totalBytes() const = 0;
  virtual size_t suppliedBytes() const = 0;
  virtual uint32_t chunksSupplied() const = 0;
  virtual const char* name() const = 0;
};

class SpotifyMemoryChunkSource final : public SpotifyMediaChunkSource {
public:
  SpotifyMemoryChunkSource(const uint8_t* data, size_t size, size_t chunkBytes, const char* sourceName)
      : data_(data), size_(size), chunkBytes_(chunkBytes), name_(sourceName ? sourceName : "memory") {}

  void reset() override {
    offset_ = 0u;
    chunks_ = 0u;
  }

  bool next(uint8_t* dst, size_t capacity, size_t& written) override {
    written = 0u;
    if (!dst || capacity == 0u || !data_) return false;
    if (offset_ >= size_) return true;
    size_t take = size_ - offset_;
    if (chunkBytes_ != 0u && take > chunkBytes_) take = chunkBytes_;
    if (take > capacity) take = capacity;
    if (take == 0u) return false;
    memcpy(dst, data_ + offset_, take);
    offset_ += take;
    ++chunks_;
    written = take;
    return true;
  }

  bool eof() const override { return offset_ >= size_; }
  size_t totalBytes() const override { return size_; }
  size_t suppliedBytes() const override { return offset_; }
  uint32_t chunksSupplied() const override { return chunks_; }
  const char* name() const override { return name_; }

private:
  const uint8_t* data_ = nullptr;
  size_t size_ = 0u;
  size_t chunkBytes_ = 0u;
  const char* name_ = "memory";
  size_t offset_ = 0u;
  uint32_t chunks_ = 0u;
};
