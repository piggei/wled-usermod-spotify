// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <Arduino.h>
#include <Wire.h>

class SpotifyES8311Codec {
public:
  explicit SpotifyES8311Codec(TwoWire* wire = &Wire) : wire_(wire) {}
  bool begin(int32_t sda, int32_t scl, uint32_t busFrequency, uint32_t sampleRate,
             uint8_t bitsPerSample, bool initializeBus = true);
  bool setVolume(uint8_t volumePercent);
  bool setSampleRate(uint32_t sampleRate);
  bool setBitsPerSample(uint8_t bits);

public:
  struct Coeff {
    uint32_t mclk, rate;
    uint8_t preDiv, preMulti, adcDiv, dacDiv, fsMode, lrckH, lrckL, bclkDiv, adcOsr, dacOsr;
  };
private:
  int findCoeff(uint32_t mclk, uint32_t rate) const;
  bool writeReg(uint8_t reg, uint8_t value);
  uint8_t readReg(uint8_t reg);
  TwoWire* wire_ = nullptr;
  uint32_t mclkHz_ = 44100u * 256u;
};
