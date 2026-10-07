#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "../audio/WavesharePcmOutput.h"

class SpotifyVorbisFixturePlayer {
public:
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
  };

  bool start(WavesharePcmOutput& output);
  void stop();
  bool active() const { return running_; }
  Telemetry telemetry() const { return telemetry_; }
  const char* lastError() const { return lastError_; }
  size_t fixtureBytes() const;
  uint32_t expectedFrames() const;

private:
  static void taskThunk(void* ctx);
  void taskLoop();
  void setError(const char* error) { lastError_ = error ? error : "unknown"; }

  WavesharePcmOutput* output_ = nullptr;
  TaskHandle_t task_ = nullptr;
  volatile bool running_ = false;
  const char* lastError_ = "idle";
  Telemetry telemetry_{};
};
