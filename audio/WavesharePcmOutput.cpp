#include "WavesharePcmOutput.h"
#include <esp_timer.h>
#include <driver/gpio.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>
#include <esp_heap_caps.h>
#include <math.h>

namespace {
constexpr TickType_t I2S_WRITE_TIMEOUT = pdMS_TO_TICKS(100);
constexpr size_t FRAMES_PER_BLOCK = SpotifyAudioConfig::DMA_FRAMES;
constexpr size_t PCM16_BYTES_PER_FRAME = 4u; // stereo signed 16-bit source
}

bool WavesharePcmOutput::begin(uint8_t volumePercent, bool sharedClockMode, bool initializeI2c) {
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
  lastError_ = "ESP32-S3 required"; return false;
#else
  if (ready_) return true;
  sharedClockMode_ = sharedClockMode;
  streamSampleRate_ = sharedClockMode_ ? SpotifyAudioConfig::SHARED_SAMPLE_RATE : SpotifyAudioConfig::SAMPLE_RATE;
  streamBitsPerSample_ = sharedClockMode_ ? SpotifyAudioConfig::SHARED_BITS_PER_SAMPLE : SpotifyAudioConfig::STANDALONE_BITS_PER_SAMPLE;

  pinMode(SpotifyAudioConfig::PA_ENABLE, OUTPUT);
  digitalWrite(SpotifyAudioConfig::PA_ENABLE, LOW);

  i2s_config_t cfg{};
  cfg.mode = (i2s_mode_t)((sharedClockMode_ ? I2S_MODE_SLAVE : I2S_MODE_MASTER) | I2S_MODE_TX);
  cfg.sample_rate = streamSampleRate_;
  cfg.bits_per_sample = streamBitsPerSample_ == 32u ? I2S_BITS_PER_SAMPLE_32BIT : I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
#if ESP_IDF_VERSION_MAJOR >= 5
  cfg.dma_desc_num = SpotifyAudioConfig::DMA_BUFFER_COUNT;
  cfg.dma_frame_num = SpotifyAudioConfig::DMA_FRAMES;
#else
  cfg.dma_buf_count = SpotifyAudioConfig::DMA_BUFFER_COUNT;
  cfg.dma_buf_len = SpotifyAudioConfig::DMA_FRAMES;
#endif
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = sharedClockMode_ ? 0 : SpotifyAudioConfig::MCLK_HZ;
  if (i2s_driver_install(port(), &cfg, 0, nullptr) != ESP_OK) {
    lastError_ = "I2S1 driver install failed"; return false;
  }

  i2s_pin_config_t pins{};
  pins.data_out_num = SpotifyAudioConfig::I2S_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  if (sharedClockMode_) {
    // Match the hardware-qualified Buzzer topology: I2S0/AudioReactive remains
    // the sole physical clock owner. I2S1 only drives DOUT and senses the live
    // BCLK/LRCK through the GPIO matrix.
    pins.mck_io_num = I2S_PIN_NO_CHANGE;
    pins.bck_io_num = I2S_PIN_NO_CHANGE;
    pins.ws_io_num = I2S_PIN_NO_CHANGE;
    if (i2s_set_pin(port(), &pins) != ESP_OK) {
      lastError_ = "I2S1 shared DOUT setup failed"; i2s_driver_uninstall(port()); return false;
    }
    gpio_input_enable((gpio_num_t)SpotifyAudioConfig::I2S_BCLK);
    gpio_input_enable((gpio_num_t)SpotifyAudioConfig::I2S_LRCK);
    esp_rom_gpio_connect_in_signal(SpotifyAudioConfig::I2S_BCLK, I2S1O_BCK_IN_IDX, false);
    esp_rom_gpio_connect_in_signal(SpotifyAudioConfig::I2S_LRCK, I2S1O_WS_IN_IDX, false);
  } else {
    pins.mck_io_num = SpotifyAudioConfig::I2S_MCLK;
    pins.bck_io_num = SpotifyAudioConfig::I2S_BCLK;
    pins.ws_io_num = SpotifyAudioConfig::I2S_LRCK;
    if (i2s_set_pin(port(), &pins) != ESP_OK ||
        i2s_set_clk(port(), streamSampleRate_,
                    streamBitsPerSample_ == 32u ? I2S_BITS_PER_SAMPLE_32BIT : I2S_BITS_PER_SAMPLE_16BIT,
                    I2S_CHANNEL_STEREO) != ESP_OK) {
      lastError_ = "I2S1 pin/clock setup failed"; i2s_driver_uninstall(port()); return false;
    }
    i2s_zero_dma_buffer(port());
  }

  if (!codec_.begin(SpotifyAudioConfig::WAVESHARE_I2C_SDA, SpotifyAudioConfig::WAVESHARE_I2C_SCL,
                    SpotifyAudioConfig::I2C_HZ, streamSampleRate_,
                    streamBitsPerSample_, initializeI2c)) {
    lastError_ = "ES8311 init failed"; i2s_driver_uninstall(port()); return false;
  }
  if (!codec_.setVolume(volumePercent)) {
    lastError_ = "ES8311 volume failed"; i2s_driver_uninstall(port()); return false;
  }

  telemetry_ = Telemetry{};
  telemetry_.internalHeapBefore = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  telemetry_.dmaCoverageUs = streamSampleRate_ > 0u
    ? (uint32_t)((uint64_t)SpotifyAudioConfig::DMA_FRAMES * SpotifyAudioConfig::DMA_BUFFER_COUNT * 1000000ull / streamSampleRate_)
    : 0u;

  ringCapacityBytes_ = SpotifyAudioConfig::PCM_RING_BYTES;
  ringStorage_ = static_cast<uint8_t*>(heap_caps_malloc(ringCapacityBytes_ + 1u, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  ringInPsram_ = ringStorage_ != nullptr;
  if (!ringStorage_) {
    ringCapacityBytes_ = SpotifyAudioConfig::PCM_RING_FALLBACK_BYTES;
    ringStorage_ = static_cast<uint8_t*>(heap_caps_malloc(ringCapacityBytes_ + 1u, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    ringInPsram_ = false;
  }
  if (!ringStorage_) {
    lastError_ = "PCM ring allocation failed"; i2s_driver_uninstall(port()); return false;
  }
  ring_ = xStreamBufferCreateStatic(ringCapacityBytes_, 1, ringStorage_, &ringStatic_);
  if (!ring_) {
    heap_caps_free(ringStorage_); ringStorage_=nullptr; ringCapacityBytes_=0;
    lastError_ = "PCM ring create failed"; i2s_driver_uninstall(port()); return false;
  }
  telemetry_.internalHeapAfter = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  running_ = true;
  pendingVolume_ = volumePercent;
  // Re-apply the saved volume from the producer task only after I2S has made
  // real progress. This mirrors the qualified Buzzer cold-boot fix and avoids
  // the observed silent-until-slider-moved state on ES8311.
  volumePending_ = true;
  // Match the qualified Buzzer scheduling model: low-priority, unpinned audio task.
  if (xTaskCreate(taskThunk, "spotify-pcm", 5120, this, 1, &task_) != pdPASS) {
    running_ = false; vStreamBufferDelete(ring_); ring_=nullptr;
    heap_caps_free(ringStorage_); ringStorage_=nullptr; ringCapacityBytes_=0;
    i2s_driver_uninstall(port()); lastError_ = "PCM task creation failed"; return false;
  }
  digitalWrite(SpotifyAudioConfig::PA_ENABLE, HIGH);
  ready_ = true; lastError_ = "none"; return true;
#endif
}

void WavesharePcmOutput::end() {
  if (!ready_ && !task_) return;
  testToneActive_ = false;
  running_ = false;
  for (uint8_t i=0; task_ && i<80; ++i) delay(2);
  digitalWrite(SpotifyAudioConfig::PA_ENABLE, LOW);
  if (!sharedClockMode_) i2s_zero_dma_buffer(port());
  i2s_driver_uninstall(port());
  if (ring_) { vStreamBufferDelete(ring_); ring_=nullptr; }
  if (ringStorage_) { heap_caps_free(ringStorage_); ringStorage_=nullptr; }
  ringCapacityBytes_=0; ringInPsram_=false;
  ready_ = false;
}

bool WavesharePcmOutput::enqueue(const uint8_t* data, size_t len, TickType_t waitTicks) {
  if (!ready_ || !ring_ || !data || !len || (len % PCM16_BYTES_PER_FRAME) != 0u) return false;
  const TickType_t started = xTaskGetTickCount();
  size_t offset = 0u;
  while (offset < len) {
    const size_t remaining = len - offset;
    const size_t chunk = remaining > sizeof(int16_t) * FRAMES_PER_BLOCK * 2u
      ? sizeof(int16_t) * FRAMES_PER_BLOCK * 2u : remaining;

    while (xStreamBufferSpacesAvailable(ring_) < chunk) {
      if (waitTicks == 0u || (xTaskGetTickCount() - started) >= waitTicks) {
        ++telemetry_.pcmIngressFailures;
        return false;
      }
      vTaskDelay(1);
    }

    const size_t sent = xStreamBufferSend(ring_, data + offset, chunk, 0);
    if (sent != chunk) {
      ++telemetry_.pcmIngressFailures;
      return false;
    }
    offset += sent;
    const size_t used = xStreamBufferBytesAvailable(ring_);
    if (used > telemetry_.highWaterBytes) telemetry_.highWaterBytes = static_cast<uint32_t>(used);
  }
  return true;
}

bool WavesharePcmOutput::enqueuePcm44100(const int16_t* stereoFrames, size_t frameCount, TickType_t waitTicks) {
  if (!ready_ || !stereoFrames || frameCount == 0u) return false;
  telemetry_.pcm44100InFrames += static_cast<uint32_t>(frameCount);

  // Standalone output already runs at cspot's native 44.1 kHz. Feed it in
  // DMA-sized chunks so the ring retains frame alignment and predictable
  // back-pressure.
  if (streamSampleRate_ == SpotifyAudioConfig::SAMPLE_RATE) {
    size_t offset = 0u;
    while (offset < frameCount) {
      const size_t remainingFrames = frameCount - offset;
      const size_t frames = remainingFrames > FRAMES_PER_BLOCK ? FRAMES_PER_BLOCK : remainingFrames;
      if (!enqueue(reinterpret_cast<const uint8_t*>(stereoFrames + offset * 2u),
                   frames * PCM16_BYTES_PER_FRAME, waitTicks)) return false;
      telemetry_.pcmStreamOutFrames += static_cast<uint32_t>(frames);
      offset += frames;
    }
    return true;
  }

  // The qualified shared AudioReactive clock is exactly 22.05 kHz. cspot's
  // sink contract is 44.1 kHz / signed 16-bit stereo, so decimate 2:1 before
  // the PSRAM ring. A two-sample box filter is intentional for this ingress
  // qualification gate; a later music-quality build may replace it with a
  // steeper low-pass while preserving this API.
  if (streamSampleRate_ != SpotifyAudioConfig::SHARED_SAMPLE_RATE) {
    ++telemetry_.pcmIngressFailures;
    return false;
  }

  alignas(4) int16_t out[FRAMES_PER_BLOCK * 2u];
  size_t outFrames = 0u;
  for (size_t i = 0; i < frameCount; ++i) {
    const int16_t left = stereoFrames[i * 2u];
    const int16_t right = stereoFrames[i * 2u + 1u];
    if (!downsampleHavePending_) {
      downsamplePendingL_ = left;
      downsamplePendingR_ = right;
      downsampleHavePending_ = true;
      continue;
    }

    out[outFrames * 2u] = static_cast<int16_t>((static_cast<int32_t>(downsamplePendingL_) + left) / 2);
    out[outFrames * 2u + 1u] = static_cast<int16_t>((static_cast<int32_t>(downsamplePendingR_) + right) / 2);
    downsampleHavePending_ = false;
    ++outFrames;

    if (outFrames == FRAMES_PER_BLOCK) {
      if (!enqueue(reinterpret_cast<const uint8_t*>(out), sizeof(out), waitTicks)) return false;
      telemetry_.pcmStreamOutFrames += static_cast<uint32_t>(outFrames);
      outFrames = 0u;
    }
  }

  if (outFrames > 0u) {
    if (!enqueue(reinterpret_cast<const uint8_t*>(out), outFrames * PCM16_BYTES_PER_FRAME, waitTicks)) return false;
    telemetry_.pcmStreamOutFrames += static_cast<uint32_t>(outFrames);
  }
  return true;
}

void WavesharePcmOutput::flushPcm() {
  downsampleHavePending_ = false;
  ringFlushPending_ = true;
}

void WavesharePcmOutput::setVolume(uint8_t volumePercent) {
  if (volumePercent > 100) volumePercent = 100;
  pendingVolume_ = volumePercent; volumePending_ = true;
}

void WavesharePcmOutput::startTestTone(uint16_t frequencyHz) {
  if (frequencyHz < 20u) frequencyHz = 20u;
  if (frequencyHz > 10000u) frequencyHz = 10000u;
  testToneHz_ = frequencyHz;
  tonePhase_ = 0u;
  testToneActive_ = true;
}

void WavesharePcmOutput::stopTestTone() {
  testToneActive_ = false;
}

size_t WavesharePcmOutput::bufferedBytes() const {
  return ring_ ? xStreamBufferBytesAvailable(ring_) : 0;
}

void WavesharePcmOutput::taskThunk(void* ctx) { static_cast<WavesharePcmOutput*>(ctx)->taskLoop(); }

void WavesharePcmOutput::taskLoop() {
  alignas(4) int16_t source16[FRAMES_PER_BLOCK * 2u];
  alignas(4) int32_t output32[FRAMES_PER_BLOCK * 2u];

  while (running_) {
    if (ringFlushPending_) {
      uint8_t discard[256];
      while (xStreamBufferReceive(ring_, discard, sizeof(discard), 0) > 0u) {}
      ringFlushPending_ = false;
      ++telemetry_.ringFlushes;
    }

    bool haveProgramAudio = false;
    if (testToneActive_) {
      const uint32_t hz = testToneHz_;
      const uint32_t step = (uint32_t)(((uint64_t)hz << 32) / streamSampleRate_);
      for (size_t i=0; i<FRAMES_PER_BLOCK; ++i) {
        const float angle = (float)tonePhase_ * (2.0f * PI / 4294967296.0f);
        const int16_t sample = (int16_t)(sinf(angle) * 9000.0f);
        source16[i*2u] = sample;
        source16[i*2u+1u] = sample;
        tonePhase_ += step;
      }
      telemetry_.testToneFrames += FRAMES_PER_BLOCK;
      haveProgramAudio = true;
    } else {
      const size_t wanted = sizeof(source16);
      size_t got = xStreamBufferReceive(ring_, source16, wanted, pdMS_TO_TICKS(20));
      if (got > 0u) {
        const size_t complete = got - (got % PCM16_BYTES_PER_FRAME);
        if (complete < wanted) {
          memset(reinterpret_cast<uint8_t*>(source16) + complete, 0, wanted - complete);
          telemetry_.ringUnderruns++;
        }
        haveProgramAudio = true;
      } else {
        memset(source16, 0, sizeof(source16));
        telemetry_.idleSilenceWrites++;
      }
    }

    const void* bytes = source16;
    size_t bytesToWrite = sizeof(source16);
    if (streamBitsPerSample_ == 32u) {
      // AudioReactive uses 32-bit I2S slots by default. Preserve cspot/test PCM
      // as signed 16-bit source data and place it in the MSBs of each 32-bit slot.
      for (size_t i=0; i<FRAMES_PER_BLOCK*2u; ++i) {
        output32[i] = static_cast<int32_t>(source16[i]) * 65536;
      }
      bytes = output32;
      bytesToWrite = sizeof(output32);
    }

    const uint32_t writeStartUs = (uint32_t)esp_timer_get_time();
    const uint32_t taskGapUs = lastWriteStartUs_ == 0u ? 0u : writeStartUs - lastWriteStartUs_;
    lastWriteStartUs_ = writeStartUs;

    size_t written=0;
    const esp_err_t err=i2s_write(port(), bytes, bytesToWrite, &written, I2S_WRITE_TIMEOUT);
    const uint32_t dt=(uint32_t)esp_timer_get_time()-writeStartUs;
    const BaseType_t core=xPortGetCoreID();
    const bool sampleStack=((telemetry_.writes + 1u) & 0x3Fu) == 0u;
    const uint32_t stackFree=sampleStack ? (uint32_t)uxTaskGetStackHighWaterMark(nullptr) : 0u;

    telemetry_.writes++;
    if (dt>telemetry_.maxWriteUs) telemetry_.maxWriteUs=dt;
    if (taskGapUs>telemetry_.maxTaskGapUs) telemetry_.maxTaskGapUs=taskGapUs;
    if (taskGapUs>telemetry_.dmaCoverageUs && telemetry_.dmaCoverageUs>0u) telemetry_.lateWrites++;
    if (core==0) telemetry_.core0Runs++; else if (core==1) telemetry_.core1Runs++;
    if (sampleStack && (telemetry_.stackMinFree==0u || stackFree<telemetry_.stackMinFree)) telemetry_.stackMinFree=stackFree;
    if (err!=ESP_OK) telemetry_.writeErrors++;
    if (err==ESP_OK && written!=bytesToWrite) telemetry_.shortWrites++;

    // Cold-boot/user volume writes are deliberately task-side and deferred
    // until the shared/standalone TX stream has successfully primed DMA.
    if (err == ESP_OK && written == bytesToWrite && volumePending_) {
      if (codec_.setVolume(pendingVolume_)) volumePending_ = false;
    }

    (void)haveProgramAudio;
  }
  task_=nullptr;
  vTaskDelete(nullptr);
}
