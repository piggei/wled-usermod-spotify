#include "SpotifyVorbisFixturePlayer.h"
#include "SpotifyVorbisFixture.h"
#include <micro_vorbis/ogg_vorbis_decoder.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <new>
#include <cstring>

namespace {
constexpr size_t OUTPUT_BUFFER_BYTES = 16u * 1024u;
constexpr TickType_t FEED_TIMEOUT = pdMS_TO_TICKS(250);
constexpr uint32_t TASK_STACK_BYTES = 12288u;
constexpr UBaseType_t TASK_PRIORITY = 1u;
}

size_t SpotifyVorbisFixturePlayer::fixtureBytes() const {
  return SpotifyVorbisFixture::SIZE;
}

uint32_t SpotifyVorbisFixturePlayer::expectedFrames() const {
  return static_cast<uint32_t>((static_cast<uint64_t>(SpotifyVorbisFixture::SAMPLE_RATE) *
                                SpotifyVorbisFixture::DURATION_MS) / 1000ull);
}

bool SpotifyVorbisFixturePlayer::start(WavesharePcmOutput& output) {
  stop();
  if (!output.ready()) {
    setError("audio-not-ready");
    return false;
  }

  output_ = &output;
  const uint32_t cumulativeStarts = telemetry_.starts + 1u;
  telemetry_ = Telemetry{};
  telemetry_.starts = cumulativeStarts;
  lastError_ = "starting";
  running_ = true;
  if (xTaskCreate(taskThunk, "spotify-vorbis", TASK_STACK_BYTES, this, TASK_PRIORITY, &task_) != pdPASS) {
    running_ = false;
    task_ = nullptr;
    output_ = nullptr;
    setError("task-create-failed");
    return false;
  }
  return true;
}

void SpotifyVorbisFixturePlayer::stop() {
  if (!task_) {
    running_ = false;
    return;
  }
  running_ = false;
  for (uint16_t i = 0u; task_ && i < 250u; ++i) vTaskDelay(pdMS_TO_TICKS(2));
  if (task_) {
    vTaskDelete(task_);
    task_ = nullptr;
  }
  output_ = nullptr;
  if (lastError_ == nullptr || strcmp(lastError_, "starting") == 0) setError("stopped");
}

void SpotifyVorbisFixturePlayer::taskThunk(void* ctx) {
  static_cast<SpotifyVorbisFixturePlayer*>(ctx)->taskLoop();
}

void SpotifyVorbisFixturePlayer::taskLoop() {
  telemetry_.internalHeapBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  telemetry_.psramBefore = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  telemetry_.internalHeapMin = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  uint8_t* pcm = static_cast<uint8_t*>(heap_caps_malloc(OUTPUT_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!pcm) pcm = static_cast<uint8_t*>(heap_caps_malloc(OUTPUT_BUFFER_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  telemetry_.outputBufferBytes = pcm ? OUTPUT_BUFFER_BYTES : 0u;

  micro_vorbis::OggVorbisDecoder* decoder = new (std::nothrow) micro_vorbis::OggVorbisDecoder(2u, false);
  if (!pcm || !decoder) {
    ++telemetry_.decodeErrors;
    setError(!pcm ? "pcm-buffer-allocation-failed" : "decoder-allocation-failed");
    if (decoder) delete decoder;
    if (pcm) heap_caps_free(pcm);
    running_ = false;
    telemetry_.internalHeapAfter = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    telemetry_.psramAfter = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    task_ = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  const uint8_t* input = SpotifyVorbisFixture::DATA;
  size_t remaining = SpotifyVorbisFixture::SIZE;
  uint8_t noProgress = 0u;
  bool formatAccepted = false;
  setError("decoding");

  while (running_ && remaining > 0u) {
    size_t consumed = 0u;
    size_t written = 0u;
    const uint32_t decodeStarted = static_cast<uint32_t>(esp_timer_get_time());
    const auto result = decoder->decode(input, remaining, pcm, OUTPUT_BUFFER_BYTES, consumed, written);
    const uint32_t decodeElapsed = static_cast<uint32_t>(esp_timer_get_time()) - decodeStarted;
    ++telemetry_.decodeCalls;
    telemetry_.lastResult = static_cast<int32_t>(result);
    if (decodeElapsed > telemetry_.maxDecodeUs) telemetry_.maxDecodeUs = decodeElapsed;

    if (result < 0) {
      ++telemetry_.decodeErrors;
      setError("decoder-error");
      break;
    }

    const auto& format = decoder->get_pcm_format();
    if (format.sample_rate() != 0u && format.num_channels() != 0u) {
      telemetry_.sampleRate = format.sample_rate();
      telemetry_.channels = static_cast<uint8_t>(format.num_channels());
      if (telemetry_.sampleRate != SpotifyVorbisFixture::SAMPLE_RATE ||
          telemetry_.channels != SpotifyVorbisFixture::CHANNELS) {
        ++telemetry_.decodeErrors;
        setError("unexpected-pcm-format");
        break;
      }
      formatAccepted = true;
    }

    if (written > 0u) {
      if (!formatAccepted || (written % (sizeof(int16_t) * 2u)) != 0u) {
        ++telemetry_.decodeErrors;
        setError("invalid-pcm-block");
        break;
      }
      const size_t frames = written / (sizeof(int16_t) * 2u);
      const uint32_t feedStarted = static_cast<uint32_t>(esp_timer_get_time());
      const bool fed = output_ && output_->enqueuePcm44100(reinterpret_cast<const int16_t*>(pcm), frames, FEED_TIMEOUT);
      const uint32_t feedElapsed = static_cast<uint32_t>(esp_timer_get_time()) - feedStarted;
      ++telemetry_.feedCalls;
      if (feedElapsed > telemetry_.maxFeedUs) telemetry_.maxFeedUs = feedElapsed;
      if (!fed) {
        ++telemetry_.feedFailures;
        setError("pcm-feed-failed");
        break;
      }
      telemetry_.pcmFrames += static_cast<uint32_t>(frames);
    }

    if (consumed > remaining) {
      ++telemetry_.decodeErrors;
      setError("decoder-consumed-overrun");
      break;
    }
    input += consumed;
    remaining -= consumed;
    telemetry_.inputConsumed += static_cast<uint32_t>(consumed);

    if (result == micro_vorbis::OGG_VORBIS_DECODER_END_OF_STREAM) {
      ++telemetry_.eosReports;
      ++telemetry_.completed;
      setError("complete-eos");
      break;
    }

    if (consumed == 0u && written == 0u) {
      if (++noProgress >= 4u) {
        ++telemetry_.decodeErrors;
        setError("decoder-no-progress");
        break;
      }
      vTaskDelay(1);
    } else {
      noProgress = 0u;
    }

    const uint32_t stackFree = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
    if (telemetry_.stackMinFree == 0u || stackFree < telemetry_.stackMinFree) telemetry_.stackMinFree = stackFree;
    const uint32_t minHeap = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (telemetry_.internalHeapMin == 0u || minHeap < telemetry_.internalHeapMin) telemetry_.internalHeapMin = minHeap;
  }

  if (running_ && remaining == 0u && telemetry_.completed == 0u && telemetry_.decodeErrors == 0u) {
    // micro-vorbis' documented basic loop terminates when the caller's input
    // buffer is exhausted; an explicit END_OF_STREAM result is not required on
    // the final audio-producing call.  For this fixed local fixture we have an
    // additional strong oracle: the complete container was consumed and the
    // exact expected PCM frame count must have been produced.
    if (formatAccepted && telemetry_.inputConsumed == SpotifyVorbisFixture::SIZE &&
        telemetry_.pcmFrames == expectedFrames()) {
      ++telemetry_.eofCompletions;
      ++telemetry_.completed;
      setError("complete-input-exhausted");
    } else {
      ++telemetry_.decodeErrors;
      setError("input-exhausted-frame-mismatch");
    }
  } else if (!running_ && telemetry_.decodeErrors == 0u && telemetry_.completed == 0u) {
    setError("stopped");
  }

  delete decoder;
  heap_caps_free(pcm);
  telemetry_.internalHeapAfter = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  telemetry_.psramAfter = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  running_ = false;
  output_ = nullptr;
  task_ = nullptr;
  vTaskDelete(nullptr);
}
