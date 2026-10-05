#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "../audio/WavesharePcmOutput.h"

// Development-only producer that mimics cspot's public PCM contract:
// signed 16-bit stereo at 44.1 kHz.  It feeds the exact same ingress method
// that the future cspot AudioSink adapter will call.
class SpotifyPcmTestSource {
public:
  struct Telemetry {
    uint32_t generatedFrames = 0;
    uint32_t feedCalls = 0;
    uint32_t feedFailures = 0;
    uint32_t maxFeedUs = 0;
    uint32_t stackMinFree = 0;
  };

  bool start(WavesharePcmOutput& output, uint16_t frequencyHz);
  void stop();
  bool active() const { return running_; }
  uint16_t frequencyHz() const { return frequencyHz_; }
  Telemetry telemetry() const { return telemetry_; }

private:
  static void taskThunk(void* ctx);
  void taskLoop();

  WavesharePcmOutput* output_ = nullptr;
  TaskHandle_t task_ = nullptr;
  volatile bool running_ = false;
  uint16_t frequencyHz_ = 1000;
  uint32_t phase_ = 0;
  Telemetry telemetry_{};
};
