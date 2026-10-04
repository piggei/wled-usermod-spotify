// SPDX-License-Identifier: Apache-2.0
#include "ES8311Codec.h"

namespace {
constexpr uint8_t ES8311_ADDRESS = 0x18;
constexpr SpotifyES8311Codec::Coeff COEFFS[] = {
  { 5644800, 22050, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
  {11289600, 44100, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
  { 5644800, 44100, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
  { 2822400, 44100, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
  { 1411200, 44100, 0x01, 0x03, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
};
}

int SpotifyES8311Codec::findCoeff(uint32_t mclk, uint32_t rate) const {
  for (size_t i = 0; i < sizeof(COEFFS)/sizeof(COEFFS[0]); ++i)
    if (COEFFS[i].mclk == mclk && COEFFS[i].rate == rate) return (int)i;
  return -1;
}

bool SpotifyES8311Codec::begin(int32_t sda, int32_t scl, uint32_t busFrequency,
                               uint32_t sampleRate, uint8_t bitsPerSample,
                               bool initializeBus) {
  if (!wire_ || sda < 0 || scl < 0) return false;
  if (initializeBus && !wire_->begin(sda, scl, busFrequency)) return false;
  wire_->beginTransmission(ES8311_ADDRESS);
  if (wire_->endTransmission() != 0) return false;

  bool ok = true;
  ok &= writeReg(0x00, 0x1F); delay(20);
  ok &= writeReg(0x00, 0x00);
  ok &= writeReg(0x00, 0x80);
  ok &= writeReg(0x01, 0x3F);
  uint8_t reg = readReg(0x06);
  reg &= ~(1U << 5); // codec slave
  ok &= writeReg(0x06, reg);
  ok &= setSampleRate(sampleRate);
  ok &= setBitsPerSample(bitsPerSample);
  ok &= writeReg(0x0D, 0x01);
  ok &= writeReg(0x0E, 0x02);
  ok &= writeReg(0x12, 0x00);
  ok &= writeReg(0x13, 0x10);
  ok &= writeReg(0x1C, 0x6A);
  ok &= writeReg(0x37, 0x08);
  return ok;
}

bool SpotifyES8311Codec::setVolume(uint8_t volumePercent) {
  if (volumePercent > 100) volumePercent = 100;
  const uint8_t v = volumePercent == 0 ? 0 : (uint8_t)(((uint16_t)volumePercent * 256u / 100u) - 1u);
  return writeReg(0x32, v);
}

bool SpotifyES8311Codec::setSampleRate(uint32_t sampleRate) {
  mclkHz_ = sampleRate * 256u;
  const int index = findCoeff(mclkHz_, sampleRate);
  if (index < 0) return false;
  const Coeff& c = COEFFS[index];
  bool ok = true;
  uint8_t reg = readReg(0x02);
  reg |= (uint8_t)((c.preDiv - 1u) << 5);
  reg |= (uint8_t)(c.preMulti << 3);
  ok &= writeReg(0x02, reg);
  ok &= writeReg(0x03, (uint8_t)((c.fsMode << 6) | c.adcOsr));
  ok &= writeReg(0x04, c.dacOsr);
  ok &= writeReg(0x05, (uint8_t)(((c.adcDiv - 1u) << 4) | (c.dacDiv - 1u)));
  reg = readReg(0x06); reg &= 0xE0;
  reg |= c.bclkDiv < 19u ? (uint8_t)(c.bclkDiv - 1u) : c.bclkDiv;
  ok &= writeReg(0x06, reg);
  reg = readReg(0x07); reg &= 0xC0; reg |= c.lrckH;
  ok &= writeReg(0x07, reg);
  ok &= writeReg(0x08, c.lrckL);
  return ok;
}

bool SpotifyES8311Codec::setBitsPerSample(uint8_t bits) {
  uint8_t code = 0;
  switch (bits) { case 16: code=3; break; case 18: code=2; break; case 20: code=1; break;
                  case 24: code=0; break; case 32: code=4; break; default: return false; }
  uint8_t a = readReg(0x09), d = readReg(0x0A);
  a = (uint8_t)((a & ~(7u << 2)) | (code << 2));
  d = (uint8_t)((d & ~(7u << 2)) | (code << 2));
  return writeReg(0x09,a) && writeReg(0x0A,d);
}

bool SpotifyES8311Codec::writeReg(uint8_t reg, uint8_t value) {
  wire_->beginTransmission(ES8311_ADDRESS); wire_->write(reg); wire_->write(value);
  return wire_->endTransmission() == 0;
}
uint8_t SpotifyES8311Codec::readReg(uint8_t reg) {
  wire_->beginTransmission(ES8311_ADDRESS); wire_->write(reg);
  if (wire_->endTransmission(false) != 0) return 0;
  if (wire_->requestFrom((uint16_t)ES8311_ADDRESS, (uint8_t)1, true) != 1u) return 0;
  return wire_->available() ? wire_->read() : 0u;
}
