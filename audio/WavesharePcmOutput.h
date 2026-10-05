#pragma once
#include <Arduino.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include "SpotifyAudioConfig.h"
#include "ES8311Codec.h"

class WavesharePcmOutput {
public:
  struct Telemetry {
    uint32_t writes=0, writeErrors=0, shortWrites=0;
    uint32_t idleSilenceWrites=0, ringUnderruns=0;
    uint32_t maxWriteUs=0, maxTaskGapUs=0, lateWrites=0;
    uint32_t core0Runs=0, core1Runs=0, stackMinFree=0;
    uint32_t highWaterBytes=0, dmaCoverageUs=0;
    uint32_t testToneFrames=0;
    uint32_t pcm44100InFrames=0, pcmStreamOutFrames=0;
    uint32_t pcmIngressFailures=0, ringFlushes=0;
    uint32_t internalHeapBefore=0, internalHeapAfter=0;
  };

  bool begin(uint8_t volumePercent, bool sharedClockMode=false, bool initializeI2c=true);
  void end();
  bool enqueue(const uint8_t* data, size_t len, TickType_t waitTicks=0);
  bool enqueuePcm44100(const int16_t* stereoFrames, size_t frameCount, TickType_t waitTicks=0);
  void flushPcm();
  void setVolume(uint8_t volumePercent);
  void startTestTone(uint16_t frequencyHz);
  void stopTestTone();
  bool testToneActive() const { return testToneActive_; }
  uint16_t testToneHz() const { return testToneHz_; }
  bool ready() const { return ready_; }
  bool sharedClockMode() const { return sharedClockMode_; }
  uint32_t sampleRate() const { return streamSampleRate_; }
  uint8_t outputBitsPerSample() const { return streamBitsPerSample_; }
  size_t bufferedBytes() const;
  size_t ringCapacityBytes() const { return ringCapacityBytes_; }
  bool ringInPsram() const { return ringInPsram_; }
  Telemetry telemetry() const { return telemetry_; }
  const char* lastError() const { return lastError_; }

private:
  static void taskThunk(void* ctx);
  void taskLoop();
  i2s_port_t port() const { return (i2s_port_t)SpotifyAudioConfig::I2S_PORT; }

  SpotifyES8311Codec codec_;
  StreamBufferHandle_t ring_ = nullptr;
  StaticStreamBuffer_t ringStatic_{};
  uint8_t* ringStorage_ = nullptr;
  size_t ringCapacityBytes_ = 0;
  bool ringInPsram_ = false;
  TaskHandle_t task_ = nullptr;
  volatile bool running_ = false;
  bool ready_ = false;
  bool sharedClockMode_ = false;
  uint32_t streamSampleRate_ = SpotifyAudioConfig::SAMPLE_RATE;
  uint8_t streamBitsPerSample_ = SpotifyAudioConfig::STANDALONE_BITS_PER_SAMPLE;
  uint8_t pendingVolume_ = 70;
  volatile bool volumePending_ = false;
  volatile bool testToneActive_ = false;
  volatile uint16_t testToneHz_ = 1000;
  uint32_t tonePhase_ = 0;
  uint32_t lastWriteStartUs_ = 0;
  bool downsampleHavePending_ = false;
  int16_t downsamplePendingL_ = 0;
  int16_t downsamplePendingR_ = 0;
  volatile bool ringFlushPending_ = false;
  const char* lastError_ = "not initialized";
  Telemetry telemetry_{};
};
