#pragma once
#include "wled.h"
#include "audio/WavesharePcmOutput.h"
#include "spotify/SpotifyZeroConfProbe.h"
#include "spotify/SpotifyPcmTestSource.h"
#include "spotify/SpotifySessionProbe.h"
#include <driver/gpio.h>
#include <esp_timer.h>

class UsermodSpotifyConnect : public Usermod {
private:
  static constexpr const char* USERMOD_VERSION = "0.1.0-dev.2g-spirc-activation";
  static constexpr const char* USERMOD_REVISION = "r3";
  bool enabled_ = false;
  bool ready_ = false;
  bool initPending_ = false;
  uint32_t initSince_ = 0;
  uint8_t volume_ = 70;
  char deviceName_[33] = "WLED Matrix";
  WavesharePcmOutput audio_;
  SpotifyZeroConfProbe zeroConf_;
  SpotifyPcmTestSource pcmTest_;
  SpotifySessionProbe sessionProbe_;

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
    if (!audioReactiveOwnsAnyClock()) return ClockMode::StandaloneMaster;
    if (sharedLrckActive()) return ClockMode::SharedSlave;
    return ClockMode::WaitingSharedClock;
  }

  void startAudio() {
    if (!enabled_ || ready_) return;
    const ClockMode mode = selectClockMode();
    if (mode == ClockMode::WaitingSharedClock) return;
    ready_ = audio_.begin(volume_, mode == ClockMode::SharedSlave, false);
  }

public:
  void setup() override {
    zeroConf_.begin(deviceName_, 80);
    sessionProbe_.begin();
    server.on(F("/spotify_info"), HTTP_ANY, [this](AsyncWebServerRequest* request) {
      zeroConf_.handleRequest(request);
    });
    server.on(F("/spotify-session"), HTTP_GET, [this](AsyncWebServerRequest* request) {
      if (!enabled_) { request->send(409, FPSTR(CONTENT_TYPE_PLAIN), F("Spotify usermod disabled")); return; }
      if (!request->hasParam("action")) {
        request->send(400, FPSTR(CONTENT_TYPE_PLAIN), F("Use ?action=probe, ?action=stop or ?action=reset"));
        return;
      }
      const String action = request->getParam("action")->value();
      if (action == "probe") {
        if (!zeroConf_.credentialsReady()) {
          request->send(409, FPSTR(CONTENT_TYPE_PLAIN), F("No cached Spotify credential; pair through Spotify first"));
          return;
        }
        if (!sessionProbe_.startNow(zeroConf_.userName(), zeroConf_.authType(), zeroConf_.authData(), zeroConf_.deviceId(), deviceName_, volume_)) {
          request->send(409, FPSTR(CONTENT_TYPE_PLAIN), String(F("Spotify session not started: state=")) + sessionProbe_.stateName() + F(" error=") + sessionProbe_.lastError());
          return;
        }
        request->send(202, FPSTR(CONTENT_TYPE_PLAIN), F("Spotify persistent AP/Mercury/SPIRC activation session started; inspect /json/info"));
        return;
      }
      if (action == "stop") {
        if (!sessionProbe_.active()) { request->send(409, FPSTR(CONTENT_TYPE_PLAIN), F("Spotify session is not active")); return; }
        sessionProbe_.requestStop();
        request->send(202, FPSTR(CONTENT_TYPE_PLAIN), F("Spotify session stop requested"));
        return;
      }
      if (action == "reset") {
        if (sessionProbe_.active()) { request->send(409, FPSTR(CONTENT_TYPE_PLAIN), F("Spotify session is active; use action=stop first")); return; }
        sessionProbe_.reset();
        request->send(200, FPSTR(CONTENT_TYPE_PLAIN), F("Spotify session telemetry reset"));
        return;
      }
      request->send(400, FPSTR(CONTENT_TYPE_PLAIN), F("Unknown action"));
    });
    server.on(F("/spotify-test"), HTTP_GET, [this](AsyncWebServerRequest* request) {
      if (!enabled_) { request->send(409, FPSTR(CONTENT_TYPE_PLAIN), F("Spotify usermod disabled")); return; }
      if (!ready_) { request->send(409, FPSTR(CONTENT_TYPE_PLAIN), String(F("Audio not ready: ")) + audio_.lastError()); return; }
      if (!request->hasParam("action")) {
        request->send(400, FPSTR(CONTENT_TYPE_PLAIN), F("Use ?action=start&tone=1000, ?action=start-pcm&tone=1000 or ?action=stop"));
        return;
      }
      const String action = request->getParam("action")->value();
      if (action == "start") {
        uint16_t hz = 1000;
        if (request->hasParam("tone")) hz = (uint16_t)constrain(request->getParam("tone")->value().toInt(), 20, 10000);
        pcmTest_.stop();
        audio_.flushPcm();
        audio_.startTestTone(hz);
        request->send(200, FPSTR(CONTENT_TYPE_PLAIN), String(F("started direct ")) + hz + F(" Hz; runs until action=stop"));
        return;
      }
      if (action == "start-pcm") {
        uint16_t hz = 1000;
        if (request->hasParam("tone")) hz = (uint16_t)constrain(request->getParam("tone")->value().toInt(), 20, 10000);
        audio_.stopTestTone();
        audio_.flushPcm();
        if (!pcmTest_.start(audio_, hz)) {
          request->send(500, FPSTR(CONTENT_TYPE_PLAIN), F("PCM 44.1 kHz test source failed to start"));
          return;
        }
        request->send(200, FPSTR(CONTENT_TYPE_PLAIN), String(F("started cspot-like PCM 44100 Hz source, tone=")) + hz + F(" Hz; runs until action=stop"));
        return;
      }
      if (action == "stop") {
        pcmTest_.stop();
        audio_.stopTestTone();
        audio_.flushPcm();
        request->send(200, FPSTR(CONTENT_TYPE_PLAIN), F("stopped"));
        return;
      }
      request->send(400, FPSTR(CONTENT_TYPE_PLAIN), F("Unknown action"));
    });
    if (enabled_) { initPending_=true; initSince_=millis(); }
  }

  void loop() override {
    sessionProbe_.loop(enabled_, zeroConf_.credentialsReady(), zeroConf_.userName(), zeroConf_.authType(), zeroConf_.authData(), zeroConf_.deviceId(), deviceName_, volume_);
    if (!enabled_) return;
    zeroConf_.loop(deviceName_);
    if (initPending_ && millis()-initSince_ > 2500u) {
      initPending_=false;
      startAudio();
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
    else s.add(F("audio ready | Spotify SPIRC transfer-ack gate"));

    JsonArray z=user.createNestedArray(F("Spotify Zeroconf"));
    z.add(String(F("state=")) + zeroConf_.stateName() + F(" | cpath=") + zeroConf_.cpath());
    z.add(String(F("mdns=")) + (zeroConf_.advertised() ? F("advertised") : F("pending")) +
          F(" attempts=") + zeroConf_.advertiseAttempts() + F(" getInfo=") + zeroConf_.getInfoRequests() +
          F(" addUser=") + zeroConf_.addUserRequests() + F(" accepted=") + zeroConf_.acceptedAddUserRequests() + F(" failed=") + zeroConf_.failedAddUserRequests());
    z.add(String(F("deviceId=")) + zeroConf_.deviceId() +
          F(" | credential=") + (zeroConf_.credentialsReady() ? F("stored") : F("none")) +
          F(" authType=") + zeroConf_.authType() + F(" authBytes=") + zeroConf_.authDataBytes());
    z.add(String(F("addUser params user=")) + (zeroConf_.lastAddUserUserPresent() ? F("present") : F("missing")) +
          F(":") + zeroConf_.authInputUserBytes() +
          F(" blob=") + (zeroConf_.lastAddUserBlobPresent() ? F("present") : F("missing")) + F(":") + zeroConf_.authInputBlobB64Bytes() +
          F(" clientKey=") + (zeroConf_.lastAddUserClientKeyPresent() ? F("present") : F("missing")) + F(":") + zeroConf_.authInputClientKeyB64Bytes());
    z.add(String(F("LoginBlob stage=")) + zeroConf_.authStage() + F(" result=") + zeroConf_.authLastResult() +
          F(" | decoded blob=") + zeroConf_.authDecodedBlobBytes() + F(" clientKey=") + zeroConf_.authDecodedClientKeyBytes() +
          F(" primary=") + zeroConf_.authPrimaryBytes() + F(" secondary=") + zeroConf_.authSecondaryBytes());
    z.add(F("LoginBlob crypto=streaming SHA1 + manual HMAC-SHA1 + software AES-192 ECB"));
    z.add(String(F("LoginBlob attempts=")) + zeroConf_.authDecodeAttempts() +
          F(" ok=") + zeroConf_.authDecodeSuccesses() + F(" persisted=") + zeroConf_.authPersistSuccesses() + F(" persistSkip=") + zeroConf_.authPersistSkips() +
          F(" credentialUserBytes=") + zeroConf_.userNameBytes() + F(" lastError=") + zeroConf_.authError());

    JsonArray n=user.createNestedArray(F("Spotify session"));
    n.add(String(F("state=")) + sessionProbe_.stateName() +
          F(" | credential=") + (zeroConf_.credentialsReady() ? F("ready") : F("missing")) +
          F(" endpoint=") + (sessionProbe_.endpoint()[0] ? sessionProbe_.endpoint() : "none"));
    n.add(String(F("AP resolve mode=")) + sessionProbe_.resolverMode() +
          F(" attempts=") + sessionProbe_.resolveAttempts() + F(" ok=") + sessionProbe_.resolveSuccesses() +
          F(" http=") + sessionProbe_.resolveHttpCode() + F(" bytes=") + sessionProbe_.resolveResponseBytes() +
          F(" fallback=") + sessionProbe_.fallbackUses());
    n.add(String(F("AP TCP attempts=")) + sessionProbe_.tcpAttempts() + F(" ok=") + sessionProbe_.tcpSuccesses() +
          F(" taskAge=") + sessionProbe_.lastDurationMs() + F("ms lastError=") + sessionProbe_.lastError());
    n.add(String(F("AP handshake attempts=")) + sessionProbe_.handshakeAttempts() + F(" ok=") + sessionProbe_.handshakeSuccesses() +
          F(" clientHello=") + sessionProbe_.clientHelloBytes() + F(" apHello=") + sessionProbe_.apHelloBytes() +
          F(" dh=") + sessionProbe_.dhSharedBytes() + F(" challenge=") + sessionProbe_.challengeResponseBytes());
    n.add(String(F("Shannon keys tx=")) + sessionProbe_.shannonSendKeyBytes() + F(" rx=") + sessionProbe_.shannonRecvKeyBytes() +
          F(" macFail=") + sessionProbe_.shannonMacFailures());
    n.add(String(F("AP auth attempts=")) + sessionProbe_.authAttempts() + F(" ok=") + sessionProbe_.authSuccesses() +
          F(" declined=") + sessionProbe_.authDeclines() + F(" request=") + sessionProbe_.authRequestBytes() +
          F(" response=") + sessionProbe_.authResponseBytes() + F(" authCmd=0x") + String(sessionProbe_.authLastCommand(), HEX));
    n.add(String(F("live starts=")) + sessionProbe_.sessionStarts() + F(" uptime=") + sessionProbe_.sessionUptimeMs() +
          F("ms rx=") + sessionProbe_.rxPackets() + F(" tx=") + sessionProbe_.txPackets() +
          F(" lastRx=0x") + String(sessionProbe_.lastRxCommand(), HEX) + F(" age=") + sessionProbe_.lastRxAgeMs() + F("ms"));
    n.add(String(F("keepalive ping=")) + sessionProbe_.pingReceived() + F(" pong=") + sessionProbe_.pongSent() +
          F(" serverTime=") + sessionProbe_.serverTimestampSeconds() + F(" country=") +
          (sessionProbe_.countryCode()[0] ? sessionProbe_.countryCode() : "--"));
    n.add(String(F("Mercury SUB attempts=")) + sessionProbe_.mercurySubAttempts() + F(" ok=") + sessionProbe_.mercurySubResponses() +
          F(" responses=") + sessionProbe_.mercuryResponses() + F(" events=") + sessionProbe_.mercuryEvents() +
          F(" seq=") + String(static_cast<uint32_t>(sessionProbe_.mercuryLastSequence())));
    n.add(String(F("Mercury lastUri=")) + (sessionProbe_.mercuryLastUri()[0] ? sessionProbe_.mercuryLastUri() : "none") +
          F(" | reconnect attempts=") + sessionProbe_.reconnectAttempts() + F(" ok=") + sessionProbe_.reconnectSuccesses());
    n.add(String(F("SPIRC hello attempts=")) + sessionProbe_.spircHelloAttempts() + F(" sent=") + sessionProbe_.spircHelloSent() +
          F(" ack=") + sessionProbe_.spircHelloAcks() + F(" bytes=") + sessionProbe_.spircHelloBytes());
    n.add(String(F("SPIRC URI root=")) + sessionProbe_.spircUriRootEvents() + F(" child=") + sessionProbe_.spircUriChildEvents() +
          F(" | rxRead stage=") + sessionProbe_.lastReadStage() + F(" declared=") + sessionProbe_.lastReadDeclaredPayload() +
          F(" max=16384 oversize=") + sessionProbe_.oversizedPackets());
    n.add(String(F("SPIRC rx=")) + sessionProbe_.spircRxFrames() + F(" remote=") + sessionProbe_.spircRemoteFrames() +
          F(" selfEcho=") + sessionProbe_.spircSelfEchoes() + F(" notify=") + sessionProbe_.spircNotifyFrames() +
          F(" load=") + sessionProbe_.spircLoadFrames() + F(" play=") + sessionProbe_.spircPlayFrames() +
          F(" pause=") + sessionProbe_.spircPauseFrames() + F(" lastType=0x") + String(sessionProbe_.spircLastType(), HEX) +
          F(" active=") + (sessionProbe_.spircRemoteActive() ? F("yes") : F("no")));
    n.add(String(F("SPIRC transfer Notify attempts=")) + sessionProbe_.spircTransferNotifyAttempts() +
          F(" sent=") + sessionProbe_.spircTransferNotifySent() + F(" ack=") + sessionProbe_.spircTransferNotifyAcks() +
          F(" bytes=") + sessionProbe_.spircTransferNotifyBytes() + F(" localActive=") +
          (sessionProbe_.spircLocalActive() ? F("yes") : F("no")));
    n.add(String(F("SPIRC Load tracks=")) + sessionProbe_.spircLastLoadTrackCount() +
          F(" position=") + sessionProbe_.spircLastLoadPositionMs() + F(" remoteStatus=") +
          sessionProbe_.spircLastLoadStatus() + F(" context=") +
          (sessionProbe_.spircLastLoadContext()[0] ? sessionProbe_.spircLastLoadContext() : "none"));
    n.add(String(F("SPIRC remote ident=")) + (sessionProbe_.spircRemoteIdent()[0] ? sessionProbe_.spircRemoteIdent() : "none") +
          F(" name=") + (sessionProbe_.spircRemoteName()[0] ? sessionProbe_.spircRemoteName() : "none"));
    n.add(String(F("AP task attempts=")) + sessionProbe_.attempts() +
          F(" heap=") + sessionProbe_.heapBefore() + F("->") + sessionProbe_.heapAfter() +
          F(" minHeap=") + sessionProbe_.minHeapSeen() + F(" stackMin=") + sessionProbe_.stackMinFree());
    n.add(F("scope=SPIRC Load -> active Notify transfer ack; metadata/audio acquisition next gate"));

    JsonArray a=user.createNestedArray(F("Spotify audio"));
    const auto t=audio_.telemetry();
    a.add(String(audio_.sharedClockMode() ? F("I2S1 slave/shared") : F("I2S1 master/standalone")) +
          F(" | ") + audio_.sampleRate() + F(" Hz | source 16-bit stereo -> output ") + audio_.outputBitsPerSample() +
          F("-bit | ES8311 | volume=") + volume_ + F(" | buffered=") + audio_.bufferedBytes());
    a.add(String(F("AR pin ownership=")) + (audioReactiveOwnsAnyClock() ? F("present") : F("none")) +
          F(" | LRCK=") + (sharedLrckActive() ? F("active") : F("inactive")) +
          F(" | policy=shared 32-bit slave when owned+clocked"));
    a.add(String(F("ring=")) + (audio_.ringInPsram() ? F("psram") : F("internal-fallback")) +
          F(" cap=") + audio_.ringCapacityBytes() + F(" highWater=") + t.highWaterBytes +
          F(" internalHeap=") + t.internalHeapBefore + F("->") + t.internalHeapAfter);
    a.add(String(F("DMA ")) + SpotifyAudioConfig::DMA_BUFFER_COUNT + F("x") + SpotifyAudioConfig::DMA_FRAMES +
          F(" coverage=") + t.dmaCoverageUs + F("us | writes=") + t.writes + F(" err=") + t.writeErrors +
          F(" short=") + t.shortWrites);
    a.add(String(F("timing maxWrite=")) + t.maxWriteUs + F("us maxGap=") + t.maxTaskGapUs +
          F("us late=") + t.lateWrites + F(" core0=") + t.core0Runs + F(" core1=") + t.core1Runs +
          F(" stackMin=") + t.stackMinFree);
    a.add(String(F("testTone=")) + (audio_.testToneActive() ? F("on") : F("off")) +
          F(" hz=") + audio_.testToneHz() + F(" frames=") + t.testToneFrames +
          F(" | idleSilence=") + t.idleSilenceWrites + F(" ringUnderrun=") + t.ringUnderruns);
    a.add(String(F("PCM ingress 44100->")) + audio_.sampleRate() +
          F(" | inFrames=") + t.pcm44100InFrames + F(" outFrames=") + t.pcmStreamOutFrames +
          F(" failures=") + t.pcmIngressFailures + F(" flushes=") + t.ringFlushes);
    const auto pt = pcmTest_.telemetry();
    a.add(String(F("pcmTest=")) + (pcmTest_.active() ? F("on") : F("off")) +
          F(" hz=") + pcmTest_.frequencyHz() + F(" generated=") + pt.generatedFrames +
          F(" feedCalls=") + pt.feedCalls + F(" feedFail=") + pt.feedFailures +
          F(" maxFeed=") + pt.maxFeedUs + F("us stackMin=") + pt.stackMinFree);
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
    const bool old=enabled_;
    getJsonValue(top[F("Enabled")], enabled_);
    const char* dn=top[F("Device name")];
    if (dn) strlcpy(deviceName_, dn, sizeof(deviceName_));
    int vol=volume_;
    getJsonValue(top[F("Volume")], vol);
    volume_=(uint8_t)constrain(vol,0,100);
    if (ready_) audio_.setVolume(volume_);
    if (old && !enabled_) { pcmTest_.stop(); audio_.stopTestTone(); audio_.end(); ready_=false; if (sessionProbe_.active()) sessionProbe_.requestStop(); else sessionProbe_.reset(); }
    if (!old && enabled_) { initPending_=true; initSince_=millis(); }
    return true;
  }

  void appendConfigData() override {
    // Keep internal config keys out of the visible WLED UI. WLED generates a
    // normal numeric input for Volume; keep it as the form backing field but
    // hide it and expose exactly one 0..100 slider tied to the same value.
    oappend(F("rl=(n,t)=>{let a=d.getElementsByName(n),e=a[0];if(!e)return;let x=e.previousSibling;if(x&&x.nodeType==3)x.nodeValue=' '+t+' '};"));
    oappend(F("rl('Spotify Connect:Enabled','Enabled:');rl('Spotify Connect:Device name','Device Name:');rl('Spotify Connect:Volume','Volume:');"));
    oappend(F("(()=>{let n='Spotify Connect:Volume',a=Array.from(d.getElementsByName(n));if(!a.length)return;let b=a[a.length-1],p=b.parentNode;a.forEach(e=>e.style.display='none');let r=d.getElementById('spotifyVolRange');if(!r){r=d.createElement('input');r.id='spotifyVolRange';r.type='range';r.min='0';r.max='100';r.step='1';r.style.width='180px';p.insertBefore(r,b)}r.value=b.value;let s=d.getElementById('spotifyVolPct');if(!s){s=d.createElement('span');s.id='spotifyVolPct';s.style.marginLeft='8px';p.insertBefore(s,b)}let u=()=>{a.forEach(e=>e.value=r.value);s.textContent=r.value+'%'};r.oninput=u;u()})();"));
  }
};

static UsermodSpotifyConnect spotifyConnectUsermod;
REGISTER_USERMOD(spotifyConnectUsermod);
