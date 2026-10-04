#include "WavesharePcmOutput.h"
#include <esp_timer.h>
#include <driver/gpio.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>
#include <esp_heap_caps.h>

bool WavesharePcmOutput::begin(uint8_t volumePercent, bool sharedClockMode, bool initializeI2c) {
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
  lastError_ = "ESP32-S3 required"; return false;
#else
  if (ready_) return true;
  sharedClockMode_ = sharedClockMode;
  streamSampleRate_ = sharedClockMode_ ? SpotifyAudioConfig::SHARED_SAMPLE_RATE : SpotifyAudioConfig::SAMPLE_RATE;

  pinMode(SpotifyAudioConfig::PA_ENABLE, OUTPUT);
  digitalWrite(SpotifyAudioConfig::PA_ENABLE, LOW);

  i2s_config_t cfg{};
  cfg.mode = (i2s_mode_t)((sharedClockMode_ ? I2S_MODE_SLAVE : I2S_MODE_MASTER) | I2S_MODE_TX);
  cfg.sample_rate = streamSampleRate_;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
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
    // Do not touch the physical shared clock output routes owned by I2S0.
    // Only configure DOUT, then sense BCLK/LRCK internally for I2S1 TX slave.
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
        i2s_set_clk(port(), streamSampleRate_, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO) != ESP_OK) {
      lastError_ = "I2S1 pin/clock setup failed"; i2s_driver_uninstall(port()); return false;
    }
    i2s_zero_dma_buffer(port());
  }

  if (!codec_.begin(SpotifyAudioConfig::WAVESHARE_I2C_SDA, SpotifyAudioConfig::WAVESHARE_I2C_SCL,
                    SpotifyAudioConfig::I2C_HZ, streamSampleRate_,
                    SpotifyAudioConfig::BITS_PER_SAMPLE, initializeI2c)) {
    lastError_ = "ES8311 init failed"; i2s_driver_uninstall(port()); return false;
  }
  if (!codec_.setVolume(volumePercent)) {
    lastError_ = "ES8311 volume failed"; i2s_driver_uninstall(port()); return false;
  }

  // Keep the large PCM queue out of scarce internal DRAM. WLED + HUB75 + BLE +
  // AudioReactive already put significant pressure on internal heap; a 64 KiB
  // dynamic StreamBuffer here can leave only a few KiB headroom during JSON/web
  // activity. The PCM payload is task-context only, so PSRAM-backed static storage
  // is safe on this ESP32-S3 target.
  telemetry_.internalHeapBefore = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  ringCapacityBytes_ = SpotifyAudioConfig::PCM_RING_BYTES;
  ringStorage_ = static_cast<uint8_t*>(heap_caps_malloc(ringCapacityBytes_ + 1u, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  ringInPsram_ = ringStorage_ != nullptr;
  if (!ringStorage_) {
    // Conservative fallback: never consume another 64 KiB of internal DRAM.
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
  if (xTaskCreatePinnedToCore(taskThunk, "spotify-pcm", 5120, this, 2, &task_, 0) != pdPASS) {
    running_ = false; vStreamBufferDelete(ring_); ring_=nullptr;
    i2s_driver_uninstall(port()); lastError_ = "PCM task creation failed"; return false;
  }
  digitalWrite(SpotifyAudioConfig::PA_ENABLE, HIGH);
  ready_ = true; lastError_ = "none"; return true;
#endif
}

void WavesharePcmOutput::end() {
  if (!ready_ && !task_) return;
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
  if (!ready_ || !ring_ || !data || !len) return false;
  size_t sent = xStreamBufferSend(ring_, data, len, waitTicks);
  size_t used = xStreamBufferBytesAvailable(ring_);
  if (used > telemetry_.highWaterBytes) telemetry_.highWaterBytes = (uint32_t)used;
  if (sent != len) telemetry_.shortWrites++;
  return sent == len;
}

void WavesharePcmOutput::setVolume(uint8_t volumePercent) {
  if (volumePercent > 100) volumePercent = 100;
  pendingVolume_ = volumePercent; volumePending_ = true;
}

size_t WavesharePcmOutput::bufferedBytes() const {
  return ring_ ? xStreamBufferBytesAvailable(ring_) : 0;
}

void WavesharePcmOutput::taskThunk(void* ctx) { static_cast<WavesharePcmOutput*>(ctx)->taskLoop(); }
void WavesharePcmOutput::taskLoop() {
  alignas(4) uint8_t chunk[2048];
  while (running_) {
    if (volumePending_) {
      codec_.setVolume(pendingVolume_); volumePending_ = false;
    }
    size_t got = xStreamBufferReceive(ring_, chunk, sizeof(chunk), pdMS_TO_TICKS(20));
    if (!got) {
      telemetry_.underruns++;
      memset(chunk, 0, sizeof(chunk)); got = sizeof(chunk);
    }
    size_t written=0;
    int64_t t0=esp_timer_get_time();
    esp_err_t err=i2s_write(port(), chunk, got, &written, pdMS_TO_TICKS(40));
    uint32_t dt=(uint32_t)(esp_timer_get_time()-t0);
    telemetry_.writes++;
    if (dt>telemetry_.maxWriteUs) telemetry_.maxWriteUs=dt;
    if (err!=ESP_OK) telemetry_.writeErrors++;
    if (written!=got) telemetry_.shortWrites++;
  }
  task_=nullptr;
  vTaskDelete(nullptr);
}
