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
    uint32_t writes=0, writeErrors=0, shortWrites=0, underruns=0;
    uint32_t maxWriteUs=0, highWaterBytes=0;
    uint32_t internalHeapBefore=0, internalHeapAfter=0;
  };

  bool begin(uint8_t volumePercent, bool sharedClockMode=false, bool initializeI2c=true);
  void end();
  bool enqueue(const uint8_t* data, size_t len, TickType_t waitTicks=0);
  void setVolume(uint8_t volumePercent);
  bool ready() const { return ready_; }
  bool sharedClockMode() const { return sharedClockMode_; }
  uint32_t sampleRate() const { return streamSampleRate_; }
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
  uint8_t pendingVolume_ = 50;
  volatile bool volumePending_ = false;
  const char* lastError_ = "not initialized";
  Telemetry telemetry_{};
};
