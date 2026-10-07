#include "SpotifyVorbisFixturePlayer.h"
#include "SpotifyVorbisFixture.h"
#include "SpotifyVorbisEncryptedFixture.h"
#include "SpotifyAudioAesCtr.h"
#include "SpotifyMediaChunkSource.h"
#include <micro_vorbis/ogg_vorbis_decoder.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <new>
#include <cstring>
#include <algorithm>

namespace {
constexpr size_t OUTPUT_BUFFER_BYTES = 16u * 1024u;
constexpr size_t INPUT_CHUNK_BYTES = 4096u;
constexpr size_t INPUT_STAGING_BYTES = 8192u;
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
  return startInternal(output, InputMode::Contiguous);
}

bool SpotifyVorbisFixturePlayer::startChunked(WavesharePcmOutput& output) {
  return startInternal(output, InputMode::Chunked);
}

bool SpotifyVorbisFixturePlayer::startAesChunked(WavesharePcmOutput& output) {
  return startInternal(output, InputMode::AesChunked);
}

bool SpotifyVorbisFixturePlayer::startInternal(WavesharePcmOutput& output, InputMode mode) {
  stop();
  if (!output.ready()) {
    setError("audio-not-ready");
    return false;
  }

  output_ = &output;
  inputMode_ = mode;
  sourceName_ = mode == InputMode::AesChunked ? "fixture-encrypted" :
                (mode == InputMode::Chunked ? "fixture-plain" : "fixture-contiguous");
  const uint32_t cumulativeStarts = telemetry_.starts + 1u;
  telemetry_ = Telemetry{};
  telemetry_.starts = cumulativeStarts;
  telemetry_.inputMode = mode;
  telemetry_.chunkBytes = mode == InputMode::Contiguous ? 0u : INPUT_CHUNK_BYTES;
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

  const bool chunked = inputMode_ != InputMode::Contiguous;
  const bool encrypted = inputMode_ == InputMode::AesChunked;

  uint8_t* pcm = static_cast<uint8_t*>(heap_caps_malloc(OUTPUT_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!pcm) pcm = static_cast<uint8_t*>(heap_caps_malloc(OUTPUT_BUFFER_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  telemetry_.outputBufferBytes = pcm ? OUTPUT_BUFFER_BYTES : 0u;

  uint8_t* staging = nullptr;
  uint8_t* sourceScratch = nullptr;
  if (chunked) {
    staging = static_cast<uint8_t*>(heap_caps_malloc(INPUT_STAGING_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!staging) staging = static_cast<uint8_t*>(heap_caps_malloc(INPUT_STAGING_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    telemetry_.stagingBytes = staging ? INPUT_STAGING_BYTES : 0u;
    if (encrypted) {
      sourceScratch = static_cast<uint8_t*>(heap_caps_malloc(INPUT_CHUNK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (!sourceScratch) sourceScratch = static_cast<uint8_t*>(heap_caps_malloc(INPUT_CHUNK_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
  }

  SpotifyMemoryChunkSource plainSource(SpotifyVorbisFixture::DATA, SpotifyVorbisFixture::SIZE,
                                       INPUT_CHUNK_BYTES, "fixture-plain");
  SpotifyMemoryChunkSource encryptedSource(SpotifyVorbisEncryptedFixture::DATA, SpotifyVorbisEncryptedFixture::SIZE,
                                           INPUT_CHUNK_BYTES, "fixture-encrypted");
  SpotifyMediaChunkSource* chunkSource = chunked ? (encrypted ? static_cast<SpotifyMediaChunkSource*>(&encryptedSource)
                                                           : static_cast<SpotifyMediaChunkSource*>(&plainSource))
                                                 : nullptr;
  if (chunkSource) chunkSource->reset();

  micro_vorbis::OggVorbisDecoder* decoder = new (std::nothrow) micro_vorbis::OggVorbisDecoder(2u, false);
  SpotifyAudioAesCtr* decryptor = encrypted ? new (std::nothrow) SpotifyAudioAesCtr() : nullptr;
  const bool decryptReady = !encrypted || (decryptor && decryptor->begin(SpotifyVorbisEncryptedFixture::KEY));
  if (encrypted) {
    telemetry_.decryptKeyBytes = SpotifyAudioAesCtr::kKeyBytes;
    telemetry_.fixedIv = true;
  }

  if (!pcm || !decoder || (chunked && !staging) || (encrypted && !sourceScratch) || !decryptReady) {
    ++telemetry_.decodeErrors;
    if (!pcm) setError("pcm-buffer-allocation-failed");
    else if (!decoder) setError("decoder-allocation-failed");
    else if (chunked && !staging) setError("input-staging-allocation-failed");
    else if (encrypted && !sourceScratch) setError("source-scratch-allocation-failed");
    else if (!decryptor) setError("aes-ctr-allocation-failed");
    else setError("aes-ctr-init-failed");
    if (decryptor) delete decryptor;
    if (decoder) delete decoder;
    if (sourceScratch) heap_caps_free(sourceScratch);
    if (staging) heap_caps_free(staging);
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
  size_t stagingUsed = 0u;
  uint8_t noProgress = 0u;
  bool formatAccepted = false;
  setError(encrypted ? "decoding-aes-chunked" : (chunked ? "decoding-chunked" : "decoding"));

  auto appendChunk = [&]() -> bool {
    if (!chunked || !chunkSource || chunkSource->eof()) return false;
    if (stagingUsed >= INPUT_STAGING_BYTES) return false;
    const size_t freeBytes = INPUT_STAGING_BYTES - stagingUsed;
    if (freeBytes == 0u) return false;

    const bool refill = telemetry_.chunkLoads > 0u;
    size_t take = 0u;
    uint8_t* sourceDst = encrypted ? sourceScratch : (staging + stagingUsed);
    const size_t sourceCapacity = encrypted ? std::min(INPUT_CHUNK_BYTES, freeBytes) : freeBytes;
    if (!chunkSource->next(sourceDst, sourceCapacity, take) || take == 0u) return false;

    if (encrypted) {
      const uint32_t decryptStarted = static_cast<uint32_t>(esp_timer_get_time());
      const bool ok = decryptor->transform(sourceScratch, staging + stagingUsed, take);
      const uint32_t decryptElapsed = static_cast<uint32_t>(esp_timer_get_time()) - decryptStarted;
      ++telemetry_.decryptCalls;
      if (decryptElapsed > telemetry_.maxDecryptUs) telemetry_.maxDecryptUs = decryptElapsed;
      if (!ok) return false;
      telemetry_.decryptBytes += static_cast<uint32_t>(take);
    }

    stagingUsed += take;
    telemetry_.sourceSupplied = static_cast<uint32_t>(chunkSource->suppliedBytes());
    telemetry_.chunkLoads = chunkSource->chunksSupplied();
    if (refill) ++telemetry_.refillWaits;
    if (stagingUsed > telemetry_.stagingHighWater) telemetry_.stagingHighWater = static_cast<uint32_t>(stagingUsed);
    return true;
  };

  if (chunked) {
    appendChunk(); // exactly one StreamChunk-sized delivery is initially visible
  }

  while (running_) {
    if (chunked) {
      if (stagingUsed == 0u) {
        if (chunkSource && !chunkSource->eof()) {
          if (!appendChunk()) {
            ++telemetry_.decodeErrors;
            setError(encrypted ? "aes-chunk-refill-failed" : "chunk-refill-failed");
            break;
          }
        } else {
          break;
        }
      }
      input = staging;
      remaining = stagingUsed;
    } else if (remaining == 0u) {
      break;
    }

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

    if (chunked) {
      if (consumed > 0u) {
        const size_t left = stagingUsed - consumed;
        if (left > 0u) memmove(staging, staging + consumed, left);
        stagingUsed = left;
        telemetry_.inputConsumed += static_cast<uint32_t>(consumed);
      }
    } else {
      input += consumed;
      remaining -= consumed;
      telemetry_.inputConsumed += static_cast<uint32_t>(consumed);
    }

    if (result == micro_vorbis::OGG_VORBIS_DECODER_END_OF_STREAM) {
      ++telemetry_.eosReports;
      const bool completeInput = telemetry_.inputConsumed == SpotifyVorbisFixture::SIZE &&
          (!chunked || telemetry_.sourceSupplied == SpotifyVorbisFixture::SIZE) &&
          (!encrypted || telemetry_.decryptBytes == SpotifyVorbisFixture::SIZE);
      if (!completeInput) {
        ++telemetry_.decodeErrors;
        setError("premature-eos");
      } else {
        ++telemetry_.completed;
        setError(encrypted ? "complete-aes-chunked-eos" : (chunked ? "complete-chunked-eos" : "complete-eos"));
      }
      break;
    }

    if (consumed == 0u && written == 0u) {
      if (chunked && chunkSource && !chunkSource->eof()) {
        // The currently delivered 4096-byte network-shaped window ended in the
        // middle of an Ogg page/packet. Preserve every unconsumed byte and make
        // exactly one additional source chunk visible before retrying.
        if (appendChunk()) {
          noProgress = 0u;
          continue;
        }
      }
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

  const bool sourceExhausted = chunked
      ? (chunkSource && chunkSource->eof() && stagingUsed == 0u)
      : (remaining == 0u);
  if (running_ && sourceExhausted && telemetry_.completed == 0u && telemetry_.decodeErrors == 0u) {
    // micro-vorbis' documented basic loop terminates when the caller's input
    // buffer is exhausted; an explicit END_OF_STREAM result is not required on
    // the final audio-producing call. For this fixed local fixture we have an
    // additional strong oracle: the complete container was consumed and the
    // exact expected PCM frame count must have been produced.
    if (formatAccepted && telemetry_.inputConsumed == SpotifyVorbisFixture::SIZE &&
        telemetry_.pcmFrames == expectedFrames() &&
        (!chunked || telemetry_.sourceSupplied == SpotifyVorbisFixture::SIZE) &&
        (!encrypted || telemetry_.decryptBytes == SpotifyVorbisFixture::SIZE)) {
      ++telemetry_.eofCompletions;
      ++telemetry_.completed;
      setError(encrypted ? "complete-aes-chunked-input-exhausted" :
                         (chunked ? "complete-chunked-input-exhausted" : "complete-input-exhausted"));
    } else {
      ++telemetry_.decodeErrors;
      setError("input-exhausted-frame-mismatch");
    }
  } else if (!running_ && telemetry_.decodeErrors == 0u && telemetry_.completed == 0u) {
    setError("stopped");
  }

  if (decryptor) delete decryptor;
  delete decoder;
  if (sourceScratch) heap_caps_free(sourceScratch);
  if (staging) heap_caps_free(staging);
  heap_caps_free(pcm);
  telemetry_.internalHeapAfter = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  telemetry_.psramAfter = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  running_ = false;
  output_ = nullptr;
  task_ = nullptr;
  vTaskDelete(nullptr);
}
