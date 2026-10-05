#pragma once
#include <Arduino.h>

namespace SpotifyAudioConfig {
static constexpr int WAVESHARE_I2C_SDA = 47;
static constexpr int WAVESHARE_I2C_SCL = 48;
static constexpr uint32_t I2C_HZ = 400000;

static constexpr int I2S_MCLK = 12;
static constexpr int I2S_BCLK = 43;
static constexpr int I2S_LRCK = 38;
static constexpr int I2S_DOUT = 21;
static constexpr int I2S_DIN  = 39;
static constexpr int PA_ENABLE = 11;

// WLED 17 Waveshare: AudioReactive owns I2S0 RX. Spotify uses I2S1 TX.
// Canonical source PCM remains signed 16-bit stereo. In shared mode the
// physical AudioReactive bus uses 32-bit I2S slots, so the output task expands
// each 16-bit sample into the MSBs of a signed 32-bit slot before i2s_write().
static constexpr int I2S_PORT = 1;
static constexpr uint32_t SAMPLE_RATE = 44100;
static constexpr uint32_t SHARED_SAMPLE_RATE = 22050;
static constexpr uint8_t SOURCE_BITS_PER_SAMPLE = 16;
static constexpr uint8_t STANDALONE_BITS_PER_SAMPLE = 16;
static constexpr uint8_t SHARED_BITS_PER_SAMPLE = 32;
static constexpr uint32_t MCLK_HZ = SAMPLE_RATE * 256u; // 11.2896 MHz

// Keep the hardware-qualified Buzzer geometry for Waveshare/HUB75 coexistence.
static constexpr uint8_t DMA_BUFFER_COUNT = 8;
static constexpr uint16_t DMA_FRAMES = 256;
static constexpr size_t PCM_RING_BYTES = 64u * 1024u;
static constexpr size_t PCM_RING_FALLBACK_BYTES = 8u * 1024u;
}
