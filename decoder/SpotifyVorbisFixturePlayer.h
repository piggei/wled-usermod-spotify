#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "../audio/WavesharePcmOutput.h"

class SpotifyVorbisFixturePlayer {
public:
  enum class InputMode : uint8_t {
    Contiguous = 0,
    Chunked = 1,
    AesChunked = 2,
  };

  struct Telemetry {
    uint32_t starts = 0;
    uint32_t completed = 0;
    uint32_t eosReports = 0;
    uint32_t eofCompletions = 0;
    uint32_t decodeCalls = 0;
    uint32_t decodeErrors = 0;
    uint32_t feedCalls = 0;
    uint32_t feedFailures = 0;
    uint32_t inputConsumed = 0;
    uint32_t pcmFrames = 0;
    uint32_t maxDecodeUs = 0;
    uint32_t maxFeedUs = 0;
    uint32_t stackMinFree = 0;
    uint32_t internalHeapBefore = 0;
    uint32_t internalHeapAfter = 0;
    uint32_t internalHeapMin = 0;
    uint32_t psramBefore = 0;
    uint32_t psramAfter = 0;
    uint32_t outputBufferBytes = 0;
    int32_t lastResult = 0;
    uint32_t sampleRate = 0;
    uint8_t channels = 0;

    // dev.2n-r8/r9 incremental-input qualification. These counters describe
    // the caller-side transport adapter only; no Spotify media bytes enter the
    // local fixture path.
    uint32_t sourceSupplied = 0;
    uint32_t chunkLoads = 0;
    uint32_t chunkBytes = 0;
    uint32_t stagingBytes = 0;
    uint32_t stagingHighWater = 0;
    uint32_t refillWaits = 0;
    InputMode inputMode = InputMode::Contiguous;

    // dev.2n-r9 local legacy-media crypto gate. The key is a public fixture
    // key only; it is never sourced from Spotify and is never printed.
    uint32_t decryptCalls = 0;
    uint32_t decryptBytes = 0;
    uint32_t maxDecryptUs = 0;
    uint8_t decryptKeyBytes = 0;
    bool fixedIv = false;
  };

  bool start(WavesharePcmOutput& output);
  bool startChunked(WavesharePcmOutput& output);
  bool startAesChunked(WavesharePcmOutput& output);
  void stop();
  bool active() const { return running_; }
  Telemetry telemetry() const { return telemetry_; }
  const char* lastError() const { return lastError_; }
  const char* sourceName() const { return sourceName_; }
  size_t fixtureBytes() const;
  uint32_t expectedFrames() const;

private:
  bool startInternal(WavesharePcmOutput& output, InputMode mode);
  static void taskThunk(void* ctx);
  void taskLoop();
  void setError(const char* error) { lastError_ = error ? error : "unknown"; }

  WavesharePcmOutput* output_ = nullptr;
  TaskHandle_t task_ = nullptr;
  volatile bool running_ = false;
  InputMode inputMode_ = InputMode::Contiguous;
  const char* lastError_ = "idle";
  const char* sourceName_ = "fixture-contiguous";
  Telemetry telemetry_{};
};
