#include "SpotifyPcmTestSource.h"
#include <esp_timer.h>
#include <math.h>

namespace {
constexpr uint32_t SOURCE_RATE = SpotifyAudioConfig::SAMPLE_RATE; // 44.1 kHz cspot contract
constexpr size_t SOURCE_FRAMES_PER_BLOCK = 512u;
constexpr TickType_t FEED_TIMEOUT = pdMS_TO_TICKS(100);
}

bool SpotifyPcmTestSource::start(WavesharePcmOutput& output, uint16_t frequencyHz) {
  stop();
  if (!output.ready()) return false;
  if (frequencyHz < 20u) frequencyHz = 20u;
  if (frequencyHz > 10000u) frequencyHz = 10000u;

  output_ = &output;
  frequencyHz_ = frequencyHz;
  phase_ = 0u;
  telemetry_ = Telemetry{};
  running_ = true;
  if (xTaskCreate(taskThunk, "spotify-pcm44", 6144, this, 1, &task_) != pdPASS) {
    running_ = false;
    task_ = nullptr;
    output_ = nullptr;
    return false;
  }
  return true;
}

void SpotifyPcmTestSource::stop() {
  if (!task_) {
    running_ = false;
    return;
  }
  running_ = false;
  for (uint16_t i = 0; task_ && i < 150u; ++i) vTaskDelay(pdMS_TO_TICKS(2));
  if (task_) {
    vTaskDelete(task_);
    task_ = nullptr;
  }
  output_ = nullptr;
}

void SpotifyPcmTestSource::taskThunk(void* ctx) {
  static_cast<SpotifyPcmTestSource*>(ctx)->taskLoop();
}

void SpotifyPcmTestSource::taskLoop() {
  alignas(4) int16_t pcm[SOURCE_FRAMES_PER_BLOCK * 2u];
  const uint32_t step = static_cast<uint32_t>((static_cast<uint64_t>(frequencyHz_) << 32) / SOURCE_RATE);

  while (running_) {
    for (size_t i = 0; i < SOURCE_FRAMES_PER_BLOCK; ++i) {
      const float angle = static_cast<float>(phase_) * (2.0f * PI / 4294967296.0f);
      const int16_t sample = static_cast<int16_t>(sinf(angle) * 9000.0f);
      pcm[i * 2u] = sample;
      pcm[i * 2u + 1u] = sample;
      phase_ += step;
    }

    const uint32_t started = static_cast<uint32_t>(esp_timer_get_time());
    const bool ok = output_ && output_->enqueuePcm44100(pcm, SOURCE_FRAMES_PER_BLOCK, FEED_TIMEOUT);
    const uint32_t elapsed = static_cast<uint32_t>(esp_timer_get_time()) - started;
    ++telemetry_.feedCalls;
    telemetry_.generatedFrames += SOURCE_FRAMES_PER_BLOCK;
    if (!ok) ++telemetry_.feedFailures;
    if (elapsed > telemetry_.maxFeedUs) telemetry_.maxFeedUs = elapsed;
    const uint32_t stackFree = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
    if (telemetry_.stackMinFree == 0u || stackFree < telemetry_.stackMinFree) telemetry_.stackMinFree = stackFree;
    if (!ok) vTaskDelay(pdMS_TO_TICKS(1));
  }

  task_ = nullptr;
  vTaskDelete(nullptr);
}
