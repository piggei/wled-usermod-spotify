#pragma once
#include "wled.h"
#include "audio/WavesharePcmOutput.h"
#include <math.h>
#include <driver/gpio.h>
#include <esp_timer.h>

// v0.1.0-dev.1: WLED17/Waveshare 44.1-kHz hardware gate.
// Intentionally does not yet embed cspot. It validates the exact audio path that
// cspot will feed in dev.2, without mixing network/protocol failures into bring-up.
class UsermodSpotifyConnect : public Usermod {
private:
  static constexpr const char* USERMOD_VERSION = "0.1.0-dev.1";
  static constexpr const char* USERMOD_REVISION = "r9";
  bool enabled_ = false;
  bool ready_ = false;
  bool initPending_ = false;
  uint32_t initSince_ = 0;
  uint8_t volume_ = 70;
  char deviceName_[33] = "WLED Matrix";
  WavesharePcmOutput audio_;

  bool arOwnsPin(int pin) const {
    return PinManager::getPinOwner(pin) == PinOwner::UM_Audioreactive;
  }
  bool audioReactiveOwnsAnyClock() const {
    return arOwnsPin(SpotifyAudioConfig::I2S_BCLK) ||
           arOwnsPin(SpotifyAudioConfig::I2S_LRCK) ||
           arOwnsPin(SpotifyAudioConfig::I2S_MCLK) ||
           arOwnsPin(SpotifyAudioConfig::I2S_DIN);
  }
  bool sharedLrckActive() const {
    const gpio_num_t pin = static_cast<gpio_num_t>(SpotifyAudioConfig::I2S_LRCK);
    gpio_input_enable(pin);
    int last = gpio_get_level(pin);
    const int64_t deadline = esp_timer_get_time() + 2500;
    while (esp_timer_get_time() < deadline) {
      const int now = gpio_get_level(pin);
      if (now != last) return true;
    }
    return false;
  }

  enum class ClockMode : uint8_t { StandaloneMaster, SharedSlave, WaitingSharedClock };

  ClockMode selectClockMode() const {
    // Match the hardware-qualified Buzzer strategy. If AudioReactive still owns
    // the shared clock pins and LRCK is physically present, follow those clocks
    // as I2S1 TX slave instead of creating a competing clock master.
    if (!audioReactiveOwnsAnyClock()) return ClockMode::StandaloneMaster;
    if (sharedLrckActive()) return ClockMode::SharedSlave;
    return ClockMode::WaitingSharedClock;
  }

  void startAudio() {
    if (!enabled_ || ready_) return;
    const ClockMode mode = selectClockMode();
    if (mode == ClockMode::WaitingSharedClock) return;
    // Board I2C is normally already active under WLED17; avoid resetting a live
    // sensor/microphone bus. In shared mode I2S0 remains the physical clock owner.
    ready_ = audio_.begin(volume_, mode == ClockMode::SharedSlave, false);
  }

  void playTestTone(uint16_t hz=1000, uint16_t ms=1500) {
    if (!ready_ || hz < 20 || hz > 10000 || ms == 0) return;
    constexpr size_t FRAMES = 256;
    int16_t pcm[FRAMES*2];
    const uint32_t rate = audio_.sampleRate();
    const uint32_t total = (rate * (uint32_t)ms) / 1000u;
    uint32_t phase = 0;
    const uint32_t step = (uint32_t)(((uint64_t)hz << 32) / rate);
    uint32_t done=0;
    while (done < total) {
      size_t n = (total - done < FRAMES) ? (size_t)(total - done) : FRAMES;
      for (size_t i=0;i<n;i++) {
        float a=(float)(phase) * (2.0f * PI / 4294967296.0f);
        int16_t s=(int16_t)(sinf(a)*9000.0f);
        pcm[i*2]=s; pcm[i*2+1]=s; phase += step;
      }
      audio_.enqueue((const uint8_t*)pcm, n*4u, pdMS_TO_TICKS(100));
      done += n;
      delay(1);
    }
  }

public:
  void setup() override {
    server.on(F("/spotify-test"), HTTP_GET, [this](AsyncWebServerRequest* request) {
      if (!enabled_) { request->send(409, FPSTR(CONTENT_TYPE_PLAIN), F("Spotify usermod disabled")); return; }
      if (!ready_) { request->send(409, FPSTR(CONTENT_TYPE_PLAIN), String(F("Audio not ready: ")) + audio_.lastError()); return; }
      uint16_t hz=1000, ms=1500;
      if (request->hasParam("hz")) hz=(uint16_t)constrain(request->getParam("hz")->value().toInt(),20,10000);
      if (request->hasParam("ms")) ms=(uint16_t)constrain(request->getParam("ms")->value().toInt(),50,5000);
      playTestTone(hz,ms);
      request->send(200, FPSTR(CONTENT_TYPE_PLAIN), F("ok"));
    });
    if (enabled_) { initPending_=true; initSince_=millis(); }
  }

  void loop() override {
    if (!enabled_) return;
    if (initPending_ && millis()-initSince_ > 2500u) {
      initPending_=false; startAudio();
    }
  }

  void addToJsonInfo(JsonObject& root) override {
    JsonObject user=root["u"];
    if (user.isNull()) user=root.createNestedObject("u");
    JsonArray v=user.createNestedArray(F("Spotify Connect"));
    v.add(String(F("v"))+USERMOD_VERSION+F("-")+USERMOD_REVISION);
    JsonArray s=user.createNestedArray(F("Spotify state"));
    if (!enabled_) s.add(F("disabled"));
    else if (!ready_ && selectClockMode() == ClockMode::WaitingSharedClock) s.add(F("waiting: AudioReactive owns clocks but LRCK inactive"));
    else if (!ready_) s.add(String(F("audio not ready: "))+audio_.lastError());
    else s.add(F("audio gate ready (cspot transport pending dev.2)"));
    JsonArray a=user.createNestedArray(F("Spotify audio"));
    auto t=audio_.telemetry();
    a.add(String(audio_.sharedClockMode() ? F("I2S1 slave/shared") : F("I2S1 master/standalone")) +
          F(" | ") + audio_.sampleRate() + F(" Hz | 16-bit stereo | ES8311 | volume=") + volume_ +
          F(" | buffered=") + audio_.bufferedBytes());
    a.add(String(F("AR pin ownership=")) + (audioReactiveOwnsAnyClock() ? F("present") : F("none")) +
          F(" | LRCK=") + (sharedLrckActive() ? F("active") : F("inactive")) +
          F(" | policy=shared-slave when owned+clocked"));
    a.add(String(F("ring=")) + (audio_.ringInPsram() ? F("psram") : F("internal-fallback")) +
          F(" cap=") + audio_.ringCapacityBytes() + F(" highWater=") + t.highWaterBytes +
          F(" internalHeap=") + t.internalHeapBefore + F("->") + t.internalHeapAfter);
    a.add(String(F("writes="))+t.writes+F(" err=")+t.writeErrors+F(" short=")+t.shortWrites+
          F(" underrun=")+t.underruns+F(" maxWriteUs=")+t.maxWriteUs);
  }

  void addToConfig(JsonObject& root) override {
    JsonObject top=root.createNestedObject(F("Spotify Connect"));
    top[F("Enabled")]=enabled_;
    top[F("Device name")]=deviceName_;
    top[F("Volume")]=volume_;
  }

  bool readFromConfig(JsonObject& root) override {
    JsonObject top=root[F("Spotify Connect")];
    if (top.isNull()) return false;
    bool old=enabled_;
    getJsonValue(top[F("Enabled")], enabled_);
    const char* dn=top[F("Device name")];
    if (dn) strlcpy(deviceName_, dn, sizeof(deviceName_));
    int vol=volume_; getJsonValue(top[F("Volume")], vol); volume_=constrain(vol,0,100);
    if (ready_) audio_.setVolume(volume_);
    if (old && !enabled_) { audio_.end(); ready_=false; }
    if (!old && enabled_) { initPending_=true; initSince_=millis(); }
    return true;
  }

  // Temporary test hook usable from another local usermod or one-line patch.
  void testTone(uint16_t hz=1000, uint16_t ms=1500) { playTestTone(hz,ms); }

};

static UsermodSpotifyConnect spotifyConnectUsermod;
REGISTER_USERMOD(spotifyConnectUsermod);
