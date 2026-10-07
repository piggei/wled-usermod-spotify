#pragma once
#include "wled.h"
#include "audio/WavesharePcmOutput.h"
#include "spotify/SpotifyZeroConfProbe.h"
#include "spotify/SpotifyPcmTestSource.h"
#include "spotify/SpotifySessionProbe.h"
#include "decoder/SpotifyVorbisFixturePlayer.h"
#include <driver/gpio.h>
#include <esp_timer.h>

class UsermodSpotifyConnect : public Usermod {
private:
  static constexpr const char* USERMOD_VERSION = "0.1.0-dev.2n-vorbis";
  static constexpr const char* USERMOD_REVISION = "r17";
  bool enabled_ = false;
  bool ready_ = false;
  bool initPending_ = false;
  uint32_t initSince_ = 0;
  uint8_t volume_ = 70;
  char deviceName_[33] = "WLED Matrix";
  WavesharePcmOutput audio_;
  SpotifyZeroConfProbe zeroConf_;
  SpotifyPcmTestSource pcmTest_;
  SpotifyVorbisFixturePlayer vorbisFixture_;
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

  void appendMetadataAudit(JsonArray& lines) const {
    using namespace spotify_metadata_audit;
    Summary audit{};
    sessionProbe_.metadataAuditSummary(audit);
    char requested[33] = {0}, returned[33] = {0}, file[41] = {0};
    if (audit.hasRequestedGid) hexId(audit.requestedGid, kGidBytes, requested, sizeof(requested));
    if (audit.primary.hasGid) hexId(audit.primary.gid, kGidBytes, returned, sizeof(returned));
    const bool parsed = audit.status == Status::Ok || audit.status == Status::Limited;
    const char* same = "unknown";
    if (parsed && audit.hasRequestedGid && audit.primary.hasGid && !audit.primary.gidConflict) {
      same = memcmp(audit.requestedGid, audit.primary.gid, kGidBytes) == 0 ? "yes" : "no";
    }
    lines.add(String(F("Metadata audit state=")) + statusName(audit.status) + F(" generation=") +
              audit.generation + F(" attempts=") + audit.attempts + F(" errors=") + audit.failures +
              F(" bytes=") + audit.inputBytes + F(" fields=") + audit.fieldsRead +
              F(" lastUs=") + audit.lastUs + F(" maxUs=") + audit.maxUs + F(" mode=observe-only"));
    lines.add(String(F("Metadata identity requestedGid=")) + (requested[0] ? requested : "none") +
              F(" returnedGid=") + (returned[0] ? returned : "none") + F(" same=") + same +
              F(" gidConflict=") + (audit.primary.gidConflict ? F("yes") : F("no")));
    const TrackView& primary = audit.primary;
    lines.add(String(F("Metadata territory country=")) + (audit.country[0] ? audit.country : "unknown") +
              F(" catalogue=") + (audit.catalogue[0] ? audit.catalogue : "unknown") +
              F(" state=") + territoryName(primary.territory) + F(" restrictions=") + primary.restrictions +
              F(" applicable=") + primary.applicable + F(" unknown=") + primary.unknownRules +
              F(" ignored=") + primary.ignoredRules + F(" allowLists=") + primary.allowLists +
              F(" denyLists=") + primary.denyLists + F(" availability=") + primary.availability +
              F(" salePeriods=") + primary.salePeriods + F(" earliestLive=") +
              (primary.earliestLive ? F("present") : F("absent")) + F(" authorization=unknown"));
    lines.add(String(F("Metadata alternatives seen=")) + audit.alternativesSeen + F(" kept=") +
              audit.alternativesStored + F(" max=") + kMaxAlternatives + F(" truncated=") +
              (audit.alternativesTruncated ? 1u : 0u) + F(" primaryFiles=") + primary.filesSeen +
              F(" primaryStored=") + primary.filesStored + F(" invalidFiles=") + primary.invalidFiles +
              F(" filesTruncated=") + (primary.filesTruncated ? 1u : 0u) + F(" relinking=not-applied"));
    for (uint8_t i = 0u; i < audit.alternativesStored; ++i) {
      TrackView alt{};
      if (!sessionProbe_.metadataAuditAlternative(audit.generation, i, alt)) {
        lines.add(F("Metadata audit snapshot=changed; read /json/info again"));
        return;
      }
      returned[0] = file[0] = '\0';
      if (alt.hasGid) hexId(alt.gid, kGidBytes, returned, sizeof(returned));
      if (alt.hasPreferredFile) hexId(alt.preferredFile, kFileIdBytes, file, sizeof(file));
      lines.add(String(F("Metadata alternative index=")) + i + F(" gid=") +
                (returned[0] ? returned : "none") + F(" files=") + alt.filesSeen + F(" stored=") +
                alt.filesStored + F(" invalidFiles=") + alt.invalidFiles + F(" truncated=") +
                (alt.filesTruncated ? 1u : 0u) + F(" gidConflict=") + (alt.gidConflict ? 1u : 0u) +
                F(" territory=") + territoryName(alt.territory) + F(" restrictions=") + alt.restrictions +
                F(" applicable=") + alt.applicable + F(" unknown=") + alt.unknownRules +
                F(" availability=") + alt.availability + F(" salePeriods=") + alt.salePeriods +
                F(" earliestLive=") + (alt.earliestLive ? 1u : 0u) +
                F(" nested=") + alt.nestedAlternatives + F(" preferredFormat=") + alt.preferredFormat +
                F(" fileId=") + (file[0] ? file : "none") + F(" selected=no"));
    }
    KeyTarget target{};
    if (!sessionProbe_.metadataAuditKeyTarget(audit.generation, target)) {
      lines.add(F("Metadata audit snapshot=changed; read /json/info again"));
      return;
    }
    requested[0] = file[0] = '\0';
    if (target.present) {
      hexId(target.gid, kGidBytes, requested, sizeof(requested));
      hexId(target.file, kFileIdBytes, file, sizeof(file));
    }
    lines.add(String(F("AudioKey target source=queue-primary gid=")) +
              (requested[0] ? requested : "none") + F(" fileId=") + (file[0] ? file : "none") +
              F(" pair=") + pairName(target.pair) + F(" sent=") + (target.sent ? F("yes") : F("no")) +
              F(" seq=") + target.sequence + F(" relinking=not-applied latch=unchanged"));
  }

  void appendKeyProbeAudit(JsonArray& lines) {
    using namespace spotify_key_probe;
    Summary probe{};
    uint32_t currentGeneration = 0u;
    bool ready = false;
    sessionProbe_.keyProbeSummary(probe, currentGeneration, ready);
    lines.add(String(F("AudioKey probe state=")) + stateName(probe.state) + F(" run=") + probe.run +
              F(" generation=") + probe.generation + F(" currentGeneration=") + currentGeneration +
              F(" targets=") + probe.targets + F(" format=") + kCompareFormat + F(" pending=") +
              (probe.pending ? F("yes") : F("no")) + F(" sent=") + probe.sent +
              F(" responses=") + probe.responses + F(" accepted=") + probe.accepted +
              F(" rejected=") + probe.rejected + F(" timeouts=") + probe.timeouts +
              F(" protoErr=") + probe.protocolErrors + F(" writeErr=") + probe.writeErrors +
              F(" reason=") + reasonName(probe.reason));
    lines.add(String(F("AudioKey probePolicy mode=manual maxTargets=")) + kMaxTargets +
              F(" runs=") + probe.runsUsed + F("/") + kMaxRunsPerBoot + F(" requests=") +
              probe.requestsUsed + F("/") + kMaxRequestsPerBoot + F(" cooldownLeft=") +
              probe.cooldownLeftMs + F("ms late=") + probe.lateResponses +
              F(" skippedAlternatives=") + probe.skippedAlternatives +
              F(" limitedAlternatives=") + probe.limitedAlternatives + F(" transportReady=") +
              (ready ? F("yes") : F("no")) +
              F(" normalCounters=separate normalLatch=unchanged keyStorage=none consumer=closed"));
    char gid[33] = {0}, file[41] = {0};
    if (probe.run) {
      spotify_metadata_audit::hexId(probe.requestedGid, 16u, gid, sizeof(gid));
      lines.add(String(F("AudioKey probeContext requestedGid=")) + gid + F(" session=") + probe.session +
                F(" authorization=unknown relinking=not-applied"));
    }
    for (uint8_t i = 0u; i < probe.targets; ++i) {
      Result item{};
      if (!sessionProbe_.keyProbeResult(probe.run, i, item)) {
        lines.add(F("AudioKey probe snapshot=changed; read /json/info again"));
        break;
      }
      spotify_metadata_audit::hexId(item.target.gid, 16u, gid, sizeof(gid));
      spotify_metadata_audit::hexId(item.target.file, 20u, file, sizeof(file));
      lines.add(String(F("AudioKey probeTarget index=")) + i + F(" source=") +
                (item.target.alternative < 0 ? F("primary") : F("alternative")) +
                F(" alternative=") + static_cast<int>(item.target.alternative) +
                F(" format=") + item.target.format + F(" gid=") + gid + F(" fileId=") + file +
                F(" pair=metadata-object territory=allowed earliestLive=") +
                (item.target.earliestLive ? F("present") : F("absent")) +
                F(" outcome=") + outcomeName(item.outcome) + F(" sent=") + (item.sent ? F("yes") : F("no")) +
                F(" seq=") + item.sequence + F(" cmd=") + item.command +
                F(" err=") + item.error0 + F(":") + item.error1 + F(" keyBytes=") + item.keyBytes +
                F(" rtt=") + item.rttMs + F("ms keyStored=no"));
    }
  }

  void handleKeyProbeGet(AsyncWebServerRequest* request) {
    // GET is read-only: opening/reloading this page cannot send a RequestKey.
    spotify_key_probe::Summary probe{};
    uint32_t generation = 0u;
    bool ready = false;
    sessionProbe_.keyProbeSummary(probe, generation, ready);
    spotify_metadata_audit::Summary metadata{};
    sessionProbe_.metadataAuditSummary(metadata);
    char gid[33] = {0};
    if (metadata.hasRequestedGid)
      spotify_metadata_audit::hexId(metadata.requestedGid, 16u, gid, sizeof(gid));
    String html;
    html.reserve(2200u);
    html = F("<!doctype html><html lang='en'><meta charset='utf-8'>"
             "<meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<title>Spotify AudioKey diagnostic</title><body><h2>AudioKey comparison</h2>"
             "<p>Manual diagnostic only. One primary and up to two alternatives, all format 1. "
             "No automatic retries or audio playback. Received keys are discarded.</p><p>Current GID: <code>");
    html += gid[0] ? gid : "none";
    html += F("</code><br>Metadata generation: "); html += metadata.generation;
    html += F("<br>AP transport ready: "); html += ready ? "yes" : "no";
    html += F("<br>Diagnostic state: "); html += spotify_key_probe::stateName(probe.state);
    html += F("<br>Runs this boot: "); html += probe.runsUsed; html += F("/4; requests: ");
    html += probe.requestsUsed; html += F("/12; cooldown remaining: "); html += probe.cooldownLeftMs;
    html += F(" ms</p><p>First select a track in Spotify and wait for its metadata and "
              "three AP canary ranges to complete. A changed track requires reloading this page.</p>"
              "<form method='post' action='/spotify-key-probe'>"
              "<input type='hidden' name='generation' value='");
    html += metadata.generation;
    html += F("'><input type='hidden' name='gid' value='");
    html += gid;
    html += F("'><button type='submit' name='action' value='start'>Start AudioKey comparison</button></form>"
              "<p>At most 4 runs and 12 additional requests per boot, 10 seconds between runs, "
              "one run per track GID. Session reset does not replenish the budget.</p>"
              "<form method='post' action='/spotify-key-probe'>"
              "<button type='submit' name='action' value='cancel'>Cancel diagnostic</button></form>"
              "<p><a href='/json/info'>Open /json/info</a></p></body></html>");
    AsyncWebServerResponse* response = request->beginResponse(200, "text/html; charset=utf-8", html);
    response->addHeader("Cache-Control", "no-store");
    response->addHeader("X-Frame-Options", "DENY");
    request->send(response);
  }

  void handleKeyProbePost(AsyncWebServerRequest* request) {
    if (!enabled_) {
      request->send(409, FPSTR(CONTENT_TYPE_PLAIN), F("Spotify usermod disabled")); return;
    }
    // Only form POST fields are accepted, not ?action=start URLs.
    if (!request->hasParam("action", true)) {
      request->send(400, FPSTR(CONTENT_TYPE_PLAIN), F("Open /spotify-key-probe and use its form")); return;
    }
    const String action = request->getParam("action", true)->value();
    if (action == "cancel") {
      const bool active = sessionProbe_.cancelKeyProbe();
      request->send(active ? 202 : 409, FPSTR(CONTENT_TYPE_PLAIN),
                    active ? F("AudioKey diagnostic cancelled; normal playback state unchanged") :
                             F("No AudioKey diagnostic is queued/running"));
      return;
    }
    uint32_t generation = 0u;
    uint8_t expectedGid[16] = {0};
    if (action != "start" || !request->hasParam("generation", true) ||
        !spotify_key_probe::parseGeneration(request->getParam("generation", true)->value().c_str(), generation) ||
        !request->hasParam("gid", true) ||
        !spotify_key_probe::parseGid(request->getParam("gid", true)->value().c_str(), expectedGid)) {
      request->send(400, FPSTR(CONTENT_TYPE_PLAIN), F("POST action=start with a valid current generation and gid")); return;
    }
    const auto reason = sessionProbe_.requestKeyProbe(generation, expectedGid);
    if (reason != spotify_key_probe::Reason::None) {
      request->send(409, FPSTR(CONTENT_TYPE_PLAIN), String(F("AudioKey diagnostic not queued: ")) +
                    spotify_key_probe::reasonName(reason) + F(". Reload /spotify-key-probe; see /json/info."));
      return;
    }
    request->send(202, FPSTR(CONTENT_TYPE_PLAIN),
                  F("AudioKey diagnostic queued (max 3 requests). Wait a few seconds, then inspect /json/info. "
                    "No live playback; no key stored. Reloading this POST cannot repeat the same track."));
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
        request->send(202, FPSTR(CONTENT_TYPE_PLAIN), F("Spotify AP/Mercury/SPIRC/media-key session started; inspect /json/info"));
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
    server.on(F("/spotify-key-probe"), HTTP_GET, [this](AsyncWebServerRequest* request) {
      handleKeyProbeGet(request);
    });
    server.on(F("/spotify-key-probe"), HTTP_POST, [this](AsyncWebServerRequest* request) {
      handleKeyProbePost(request);
    });
    server.on(F("/spotify-test"), HTTP_GET, [this](AsyncWebServerRequest* request) {
      if (!enabled_) { request->send(409, FPSTR(CONTENT_TYPE_PLAIN), F("Spotify usermod disabled")); return; }
      if (!ready_) { request->send(409, FPSTR(CONTENT_TYPE_PLAIN), String(F("Audio not ready: ")) + audio_.lastError()); return; }
      if (!request->hasParam("action")) {
        request->send(400, FPSTR(CONTENT_TYPE_PLAIN), F("Use ?action=start&tone=1000, ?action=start-pcm&tone=1000, ?action=start-vorbis, ?action=start-vorbis-chunked, ?action=start-vorbis-aes-chunked or ?action=stop"));
        return;
      }
      const String action = request->getParam("action")->value();
      if (action == "start") {
        uint16_t hz = 1000;
        if (request->hasParam("tone")) hz = (uint16_t)constrain(request->getParam("tone")->value().toInt(), 20, 10000);
        vorbisFixture_.stop();
        pcmTest_.stop();
        audio_.flushPcm();
        audio_.startTestTone(hz);
        request->send(200, FPSTR(CONTENT_TYPE_PLAIN), String(F("started direct ")) + hz + F(" Hz; runs until action=stop"));
        return;
      }
      if (action == "start-pcm") {
        uint16_t hz = 1000;
        if (request->hasParam("tone")) hz = (uint16_t)constrain(request->getParam("tone")->value().toInt(), 20, 10000);
        vorbisFixture_.stop();
        audio_.stopTestTone();
        audio_.flushPcm();
        if (!pcmTest_.start(audio_, hz)) {
          request->send(500, FPSTR(CONTENT_TYPE_PLAIN), F("PCM 44.1 kHz test source failed to start"));
          return;
        }
        request->send(200, FPSTR(CONTENT_TYPE_PLAIN), String(F("started cspot-like PCM 44100 Hz source, tone=")) + hz + F(" Hz; runs until action=stop"));
        return;
      }
      if (action == "start-vorbis") {
        pcmTest_.stop();
        audio_.stopTestTone();
        audio_.flushPcm();
        if (!vorbisFixture_.start(audio_)) {
          request->send(500, FPSTR(CONTENT_TYPE_PLAIN), String(F("local Ogg/Vorbis fixture failed to start: ")) + vorbisFixture_.lastError());
          return;
        }
        request->send(200, FPSTR(CONTENT_TYPE_PLAIN), F("started local 44.1 kHz stereo Ogg/Vorbis fixture; one-shot playback"));
        return;
      }
      if (action == "start-vorbis-chunked") {
        pcmTest_.stop();
        audio_.stopTestTone();
        audio_.flushPcm();
        if (!vorbisFixture_.startChunked(audio_)) {
          request->send(500, FPSTR(CONTENT_TYPE_PLAIN), String(F("chunked local Ogg/Vorbis fixture failed to start: ")) + vorbisFixture_.lastError());
          return;
        }
        request->send(200, FPSTR(CONTENT_TYPE_PLAIN), F("started chunked local Ogg/Vorbis fixture: 4096-byte source deliveries; one-shot playback"));
        return;
      }
      if (action == "start-vorbis-aes-chunked") {
        pcmTest_.stop();
        audio_.stopTestTone();
        audio_.flushPcm();
        if (!vorbisFixture_.startAesChunked(audio_)) {
          request->send(500, FPSTR(CONTENT_TYPE_PLAIN), String(F("AES-CTR chunked local Ogg/Vorbis fixture failed to start: ")) + vorbisFixture_.lastError());
          return;
        }
        request->send(200, FPSTR(CONTENT_TYPE_PLAIN), F("started locally AES-128-CTR encrypted chunked Ogg/Vorbis fixture: 4096-byte decrypt deliveries; one-shot playback"));
        return;
      }
      if (action == "stop") {
        vorbisFixture_.stop();
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
    else s.add(F("audio ready | Spotify media-key hardening + local Vorbis gate"));

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
    n.add(String(F("AP identity product=")) + sessionProbe_.clientProductClass() +
          F(" platform=") + sessionProbe_.clientPlatformClass() + F(" spotifyVersion=0x10800000000 cpu=") +
          sessionProbe_.authCpuClass() + F(" os=") + sessionProbe_.authOsClass() + F(" system=") +
          sessionProbe_.authSystemName() + F(" clientVersion=") + sessionProbe_.authClientVersion() +
          F(" authType=") + sessionProbe_.credentialAuthType() + F(" mode=measurement-only"));
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
          F(" pause=") + sessionProbe_.spircPauseFrames() + F(" playPause=") + sessionProbe_.spircPlayPauseFrames() +
          F(" seek=") + sessionProbe_.spircSeekFrames() + F(" prev=") + sessionProbe_.spircPrevFrames() +
          F(" next=") + sessionProbe_.spircNextFrames() + F(" replace=") + sessionProbe_.spircReplaceFrames() +
          F(" playSelect=") + sessionProbe_.spircPlaySelectFrames() +
          F(" byIndex=") + sessionProbe_.spircPlaySelectByIndex() + F(" lastType=0x") + String(sessionProbe_.spircLastType(), HEX) +
          F(" active=") + (sessionProbe_.spircRemoteActive() ? F("yes") : F("no")));
    n.add(String(F("SPIRC transfer Notify attempts=")) + sessionProbe_.spircTransferNotifyAttempts() +
          F(" sent=") + sessionProbe_.spircTransferNotifySent() + F(" ack=") + sessionProbe_.spircTransferNotifyAcks() +
          F(" bytes=") + sessionProbe_.spircTransferNotifyBytes() + F(" localActive=") +
          (sessionProbe_.spircLocalActive() ? F("yes") : F("no")));
    n.add(String(F("SPIRC blocked Notify attempts=")) + sessionProbe_.spircBlockedNotifyAttempts() +
          F(" sent=") + sessionProbe_.spircBlockedNotifySent() + F(" ack=") + sessionProbe_.spircBlockedNotifyAcks() +
          F(" bytes=") + sessionProbe_.spircBlockedNotifyBytes() + F(" emptyLoadIgnored=") +
          sessionProbe_.spircEmptyLoadsIgnored() + F(" duplicateLoadIgnored=") +
          sessionProbe_.spircDuplicateLoadsIgnored() + F(" duplicateLoadAcked=") +
          sessionProbe_.spircDuplicateLoadsAcked() + F(" duplicateCpsUnknown=") +
          sessionProbe_.spircDuplicateLoadsWithUnknownContext());
    n.add(String(F("SPIRC control Notify sent=")) + sessionProbe_.spircControlNotifySent() +
          F(" ack=") + sessionProbe_.spircControlNotifyAcks() + F(" bytes=") +
          sessionProbe_.spircControlNotifyBytes() + F(" commandAcks=") + sessionProbe_.spircCommandAcksSent() +
          F(" recipientIgnored=") + sessionProbe_.spircRecipientIgnored());
    n.add(String(F("SPIRC contextPlayer frames=")) + sessionProbe_.spircContextPlayerFrames() +
          F(" bytes=") + sessionProbe_.spircContextPlayerBytes() + F(" encoding=") +
          (sessionProbe_.spircContextPlayerEncoding()[0] ? sessionProbe_.spircContextPlayerEncoding() : "none") +
          F(" endpoint=") +
          (sessionProbe_.spircContextPlayerEndpoint()[0] ? sessionProbe_.spircContextPlayerEndpoint() : "none") +
          F(" play=") + sessionProbe_.spircContextPlayerPlay() + F(" select=") +
          sessionProbe_.spircContextPlayerSelect() + F(" byUid=") + sessionProbe_.spircContextPlayerByUid() +
          F(" byUri=") + sessionProbe_.spircContextPlayerByUri() + F(" byIndex=") +
          sessionProbe_.spircContextPlayerByIndex() + F(" byScan=") + sessionProbe_.spircContextPlayerByScan() +
          F(" byInflate=") + sessionProbe_.spircContextPlayerByInflate() + F(" bySkip=") +
          sessionProbe_.spircContextPlayerBySkipScan() + F(" unresolved=") +
          sessionProbe_.spircContextPlayerUnresolved());
    n.add(String(F("SPIRC contextDiag lastBytes=")) + sessionProbe_.spircContextPlayerLastBytes() +
          F(" hash=0x") + String(sessionProbe_.spircContextPlayerLastHash(), HEX) + F(" prefix=") +
          (sessionProbe_.spircContextPlayerPrefix()[0] ? sessionProbe_.spircContextPlayerPrefix() : "none") +
          F(" magic=") + (sessionProbe_.spircContextPlayerMagic()[0] ? sessionProbe_.spircContextPlayerMagic() : "none") +
          F(" printable=") + sessionProbe_.spircContextPlayerPrintablePct() + F("% proto=") +
          (sessionProbe_.spircContextPlayerProtoValid() ? F("yes") : F("no")) + F(" fields=") +
          sessionProbe_.spircContextPlayerProtoFields() + F(" lenFields=") +
          sessionProbe_.spircContextPlayerProtoLengthFields() + F(" map=") +
          (sessionProbe_.spircContextPlayerProtoMap()[0] ? sessionProbe_.spircContextPlayerProtoMap() : "none"));
    n.add(String(F("SPIRC contextScan queueMatches=")) + sessionProbe_.spircContextPlayerQueueMatches() +
          F(" nonCurrent=") + sessionProbe_.spircContextPlayerNonCurrentMatches() + F(" uniqueIndex=") +
          sessionProbe_.spircContextPlayerUniqueIndex());
    n.add(String(F("SPIRC contextQueue refs=")) + sessionProbe_.spircContextQueueRefs() +
          F(" gidOnly=") + sessionProbe_.spircContextQueueGidOnly() + F(" nativeUri=") +
          sessionProbe_.spircContextQueueNativeUri() + F(" canonical=") +
          sessionProbe_.spircContextQueueCanonical());
    n.add(String(F("SPIRC contextSkip objects=")) + sessionProbe_.spircContextSkipObjects() +
          F(" uid=") + sessionProbe_.spircContextSkipUidCandidates() + F(" uri=") +
          sessionProbe_.spircContextSkipUriCandidates() + F(" index=") +
          sessionProbe_.spircContextSkipIndexCandidates() + F(" uidResolved=") +
          sessionProbe_.spircContextSkipUidResolved() + F(" queueResolved=") +
          sessionProbe_.spircContextSkipQueueResolved() + F(" indexValidated=") +
          sessionProbe_.spircContextSkipIndexValidated() + F(" indexIgnored=") +
          sessionProbe_.spircContextSkipIndexIgnored() + F(" uniqueIndex=") +
          sessionProbe_.spircContextSkipUniqueIndex() + F(" ambiguous=") +
          sessionProbe_.spircContextSkipAmbiguous());
    n.add(String(F("SPIRC timing resolveLast=")) + sessionProbe_.spircContextResolveLastUs() +
          F("us resolveMax=") + sessionProbe_.spircContextResolveMaxUs() + F("us applyLast=") +
          sessionProbe_.spircSelectionApplyLastUs() + F("us applyMax=") +
          sessionProbe_.spircSelectionApplyMaxUs() + F("us poll=50ms"));
    n.add(String(F("SPIRC contextInflate attempts=")) + sessionProbe_.spircContextInflateAttempts() +
          F(" ok=") + sessionProbe_.spircContextInflateOk() + F(" failures=") +
          sessionProbe_.spircContextInflateFailures() + F(" status=") +
          (sessionProbe_.spircContextInflateStatus()[0] ? sessionProbe_.spircContextInflateStatus() : "none") +
          F(" bytes=") + sessionProbe_.spircContextInflateBytes() + F(" printable=") +
          sessionProbe_.spircContextInflatePrintablePct() + F("% encoding=") +
          (sessionProbe_.spircContextInflateEncoding()[0] ? sessionProbe_.spircContextInflateEncoding() : "none"));
    n.add(String(F("SPIRC playback status=")) + sessionProbe_.spircLastLoadStatus() + F(" clock=") +
          (sessionProbe_.spircPlaybackClockRunning() ? F("running") : F("held")) + F(" basePos=") +
          sessionProbe_.spircPlaybackClockBasePositionMs());
    n.add(String(F("SPIRC state tracks=")) + sessionProbe_.spircStateTrackRefCount() +
          F(" trackBytes=") + sessionProbe_.spircStateTrackRefBytes() + F(" truncated=") +
          sessionProbe_.spircStateTrackRefsTruncated() + F(" fallbackRefs=") +
          sessionProbe_.spircStateFallbackTrackRefs());
    n.add(String(F("SPIRC Load tracks=")) + sessionProbe_.spircLastLoadTrackCount() +
          F(" position=") + sessionProbe_.spircLastLoadPositionMs() + F(" remoteStatus=") +
          sessionProbe_.spircLastLoadStatus() + F(" context=") +
          (sessionProbe_.spircLastLoadContext()[0] ? sessionProbe_.spircLastLoadContext() : "none"));
    n.add(String(F("TrackRef index=")) + sessionProbe_.trackRefIndex() + F(" gid=") +
          (sessionProbe_.trackRefGidHex()[0] ? sessionProbe_.trackRefGidHex() : "none") + F(" uri=") +
          (sessionProbe_.trackRefUri()[0] ? sessionProbe_.trackRefUri() : "none"));
    n.add(String(F("Metadata GET attempts=")) + sessionProbe_.metadataRequests() + F(" responses=") +
          sessionProbe_.metadataResponses() + F(" ok=") + sessionProbe_.metadataSuccesses() +
          F(" parseFail=") + sessionProbe_.metadataParseFailures() + F(" status=") +
          sessionProbe_.metadataLastStatus() + F(" bytes=") + sessionProbe_.metadataLastBytes() +
          F(" rtt=") + sessionProbe_.metadataLastRoundTripMs() + F("ms maxRtt=") +
          sessionProbe_.metadataMaxRoundTripMs() + F("ms"));
    n.add(String(F("Track title=")) + (sessionProbe_.metadataTitle()[0] ? sessionProbe_.metadataTitle() : "none") +
          F(" artist=") + (sessionProbe_.metadataArtists()[0] ? sessionProbe_.metadataArtists() : "none"));
    n.add(String(F("Track album=")) + (sessionProbe_.metadataAlbum()[0] ? sessionProbe_.metadataAlbum() : "none") +
          F(" duration=") + sessionProbe_.metadataDurationMs() + F("ms covers=") +
          sessionProbe_.metadataCoverCount() + F(" coverId=") +
          (sessionProbe_.metadataCoverIdHex()[0] ? sessionProbe_.metadataCoverIdHex() : "none"));
    n.add(String(F("Track audioFiles=")) + sessionProbe_.metadataAudioFileCount() + F(" preferredFormat=") +
          sessionProbe_.metadataPreferredFormat() + F(" fileId=") +
          (sessionProbe_.metadataPreferredFileIdHex()[0] ? sessionProbe_.metadataPreferredFileIdHex() : "none"));
    appendMetadataAudit(n);
    appendKeyProbeAudit(n);
    n.add(String(F("AudioKey requests=")) + sessionProbe_.audioKeyRequests() + F(" responses=") +
          sessionProbe_.audioKeyResponses() + F(" ok=") + sessionProbe_.audioKeySuccesses() +
          F(" errors=") + sessionProbe_.audioKeyErrors() + F(" timeouts=") + sessionProbe_.audioKeyTimeouts() +
          F(" rejects=") + sessionProbe_.audioKeyServiceRejects() + F(" protoErr=") +
          sessionProbe_.audioKeyProtocolErrors() + F(" stale=") + sessionProbe_.audioKeyStaleResponses() +
          F(" trackCancel=") + sessionProbe_.audioKeyTrackChangeCancels() + F(" pending=") +
          (sessionProbe_.audioKeyPending() ? F("yes") : F("no")));
    n.add(String(F("AudioKey seq=")) + sessionProbe_.audioKeyLastSequence() + F(" requestBytes=") +
          sessionProbe_.audioKeyRequestBytes() + F(" keyBytes=") + sessionProbe_.audioKeyBytes() +
          F(" lastCmd=0x") + String(sessionProbe_.audioKeyLastCommand(), HEX) + F(" err=") +
          sessionProbe_.audioKeyError0() + F(":") + sessionProbe_.audioKeyError1());
    n.add(String(F("AudioKey candidate=")) +
          (sessionProbe_.audioKeyCandidateCount() ? sessionProbe_.audioKeyCandidateIndex() + 1u : 0u) + F("/") +
          sessionProbe_.audioKeyCandidateCount() + F(" format=") + sessionProbe_.audioKeyCandidateFormat() +
          F(" advances=") + sessionProbe_.audioKeyCandidateAdvances() + F(" truncated=") +
          sessionProbe_.audioKeyCandidateTruncated() + F(" scan=") +
          (sessionProbe_.mediaKeyServiceBlocked() && sessionProbe_.mediaKeySuppressedTracks() > 0u
               ? F("suppressed") : F("active")));
    String candidateTrace(F("AudioKey candidates"));
    for (uint8_t i = 0u; i < sessionProbe_.audioKeyCandidateCount(); ++i) {
      candidateTrace += F(" [");
      candidateTrace += String(i);
      candidateTrace += F(" f=");
      candidateTrace += String(sessionProbe_.audioKeyCandidateFormatAt(i));
      if (sessionProbe_.audioKeyCandidateTimedOutAt(i)) {
        candidateTrace += F(" timeout");
      } else {
        const uint8_t cmd = sessionProbe_.audioKeyCandidateResultCommandAt(i);
        candidateTrace += F(" cmd=0x");
        candidateTrace += String(cmd, HEX);
        if (cmd == 0x0eu) {
          candidateTrace += F(" err=");
          candidateTrace += String(sessionProbe_.audioKeyCandidateError0At(i));
          candidateTrace += F(":");
          candidateTrace += String(sessionProbe_.audioKeyCandidateError1At(i));
        }
      }
      candidateTrace += F("]");
    }
    n.add(candidateTrace);
    n.add(String(F("MediaKey state=")) + sessionProbe_.mediaKeyStateName() + F(" blocked=") +
          (sessionProbe_.mediaKeyServiceBlocked() ? F("yes") : F("no")) + F(" blockEvents=") +
          sessionProbe_.mediaKeyBlockEvents() + F(" suppressedTracks=") + sessionProbe_.mediaKeySuppressedTracks() +
          F(" blockErr=") + sessionProbe_.mediaKeyBlockError0() + F(":") + sessionProbe_.mediaKeyBlockError1());
    n.add(String(F("ProductInfo packets=")) + sessionProbe_.productInfoPackets() + F(" bytes=") +
          sessionProbe_.productInfoBytes() + F(" hash=0x") + String(sessionProbe_.productInfoHash(), HEX) +
          F(" xml=") + (sessionProbe_.productInfoXmlLike() ? F("yes") : F("no")) + F(" headUrl=") +
          (sessionProbe_.headFileTemplateAvailable() ? F("yes") : F("no")) + F(" scheme=") +
          sessionProbe_.headFileScheme());
    n.add(String(F("ProductInfo attrs type=")) + sessionProbe_.productInfoType() + F(" catalogue=") +
          sessionProbe_.productInfoCatalogue() + F(" playerLicense=") + sessionProbe_.productInfoPlayerLicense() +
          F(" headFiles=") + sessionProbe_.productInfoHeadFiles());
    n.add(String(F("ProductInfo keyCaps onDemand=")) + sessionProbe_.productInfoOnDemand() +
          F(" highBitrate=") + sessionProbe_.productInfoHighBitrate() + F(" unrestricted=") +
          sessionProbe_.productInfoUnrestricted() + F(" mobile=") + sessionProbe_.productInfoMobile() +
          F(" prefetchKeys=") + sessionProbe_.productInfoPrefetchKeys() + F(" keyMemory=") +
          sessionProbe_.productInfoKeyMemoryCacheMode() + F(" keyCacheMax=") +
          sessionProbe_.productInfoKeyCachingMaxCount());
    n.add(F("AudioKey identityAudit requestWire=frozen fileId20+gid16+be32seq+be16zero identityMutation=none"));
    n.add(String(F("MediaHead attempts=")) + sessionProbe_.mediaHeadAttempts() + F(" ok=") +
          sessionProbe_.mediaHeadSuccesses() + F(" skipped=") + sessionProbe_.mediaHeadSkipped() +
          F(" http=") + sessionProbe_.mediaHeadHttpCode() + F(" len=") +
          sessionProbe_.mediaHeadContentLength() + F(" bytes=") + sessionProbe_.mediaHeadBytes() +
          F(" range=") + (sessionProbe_.mediaHeadRangeHonored() ? F("yes") : F("no")) +
          F(" ogg=") + (sessionProbe_.mediaHeadOggCapture() ? F("yes") : F("no")) +
          F(" unsupportedScheme=") + sessionProbe_.mediaHeadUnsupportedScheme());
    n.add(String(F("MediaHead lastError=")) + sessionProbe_.mediaHeadLastError());
    n.add(String(F("AP Stream attempts=")) + sessionProbe_.apStreamAttempts() + F(" ok=") +
          sessionProbe_.apStreamSuccesses() + F(" failures=") + sessionProbe_.apStreamFailures() +
          F(" timeouts=") + sessionProbe_.apStreamTimeouts() + F(" protoErr=") +
          sessionProbe_.apStreamProtocolErrors() + F(" stale=") + sessionProbe_.apStreamStalePackets() +
          F(" postComplete=") + sessionProbe_.apStreamPostCompletePackets() + F(" trackCancel=") +
          sessionProbe_.apStreamTrackChangeCancels() + F(" pending=") +
          (sessionProbe_.apStreamPending() ? F("yes") : F("no")));
    n.add(String(F("AP Stream channel=")) + sessionProbe_.apStreamChannelId() + F(" requestBytes=") +
          sessionProbe_.apStreamRequestBytes() + F(" requested=") + sessionProbe_.apStreamRequestedBytes() +
          F(" totalRequested=") + sessionProbe_.apStreamTotalRequestedBytes() + F(" probe=") +
          sessionProbe_.apStreamCompletedProbes() + F("/") + sessionProbe_.apStreamProbeCount() +
          F(" offset=") + sessionProbe_.apStreamCurrentOffsetBytes() + F(" responsePackets=") +
          sessionProbe_.apStreamResponsePackets() + F(" lastCmd=0x") +
          String(sessionProbe_.apStreamLastCommand(), HEX) + F(" failureCode=") +
          sessionProbe_.apStreamFailureCode());
    n.add(String(F("AP Stream headers=")) + sessionProbe_.apStreamHeaderCount() + F(" headerBytes=") +
          sessionProbe_.apStreamHeaderBytes() + F(" headerDone=") +
          (sessionProbe_.apStreamHeadersComplete() ? F("yes") : F("no")) + F(" fileBytes=") +
          sessionProbe_.apStreamReportedFileBytes() + F(" dataPackets=") + sessionProbe_.apStreamDataPackets() +
          F(" dataBytes=") + sessionProbe_.apStreamDataBytes() + F(" format=") +
          sessionProbe_.apStreamCandidateFormat());
    n.add(String(F("AP Stream lastError=")) + sessionProbe_.apStreamLastError());
    n.add(String(F("AP Stream sourceGate chunks=")) + sessionProbe_.apStreamSourceChunks() + F("/") +
          sessionProbe_.apStreamProbeCount() + F(" mismatch=") + sessionProbe_.apStreamSourceChunkMismatches() +
          F(" bytes=") + sessionProbe_.apStreamCipherBytesHashed() + F(" hash=0x") +
          String(sessionProbe_.apStreamCipherHash(), HEX) + F(" ready=") +
          (sessionProbe_.apStreamSourceContractReady() ? F("yes") : F("no")) +
          F(" telemetryStorage=none"));
    n.add(String(F("AP Stream liveSource source=ap-encrypted-canary contract=MediaChunkSource ready=")) +
          (sessionProbe_.apStreamLiveSourceReady() ? F("yes") : F("no")) + F(" valid=") +
          (sessionProbe_.apStreamLiveSourceValid() ? F("yes") : F("no")) + F(" retained=") +
          sessionProbe_.apStreamLiveSourceRetainedBytes() + F("/") +
          sessionProbe_.apStreamLiveSourceCapacityBytes() + F(" chunks=") +
          sessionProbe_.apStreamLiveSourceChunks() + F(" fragments=") +
          sessionProbe_.apStreamLiveSourceFragments() + F(" failures=") +
          sessionProbe_.apStreamLiveSourceFailures() + F(" storage=") +
          sessionProbe_.apStreamLiveSourceStorage() + F(" consumer=closed keyGate=") +
          (sessionProbe_.apStreamLiveKeyGateEligible() ? F("eligible") : F("blocked")));
    n.add(String(F("AP Stream liveVerify attempts=")) + sessionProbe_.apStreamLiveVerifyAttempts() +
          F(" ok=") + sessionProbe_.apStreamLiveVerifySuccesses() + F(" fail=") +
          sessionProbe_.apStreamLiveVerifyFailures() + F(" calls=") +
          sessionProbe_.apStreamLiveVerifyReadCalls() + F(" chunks=") +
          sessionProbe_.apStreamLiveVerifyChunks() + F(" bytes=") +
          sessionProbe_.apStreamLiveVerifyBytes() + F(" hash=0x") +
          String(sessionProbe_.apStreamLiveVerifyHash(), HEX) + F(" hashMatch=") +
          (sessionProbe_.apStreamLiveVerifyHashMatch() ? F("yes") : F("no")) + F(" eof=") +
          (sessionProbe_.apStreamLiveVerifyEof() ? F("yes") : F("no")) + F(" rewind=") +
          (sessionProbe_.apStreamLiveVerifyRewound() ? F("yes") : F("no")) +
          F(" decrypt=closed"));
    n.add(String(F("AP Continuous state=")) + sessionProbe_.apContinuousStateName() +
          F(" target=") + sessionProbe_.apContinuousTargetBytes() + F(" received=") +
          sessionProbe_.apContinuousDataBytes() + F(" consumed=") +
          sessionProbe_.apContinuousConsumerBytes() + F(" ranges=") +
          sessionProbe_.apContinuousCompletedRanges() + F("/") + sessionProbe_.apContinuousRangeCount() +
          F(" pending=") + (sessionProbe_.apContinuousPending() ? F("yes") : F("no")));
    n.add(String(F("AP Continuous transport attempts=")) + sessionProbe_.apContinuousAttempts() +
          F(" ok=") + sessionProbe_.apContinuousSuccesses() + F(" failures=") +
          sessionProbe_.apContinuousFailures() + F(" timeouts=") + sessionProbe_.apContinuousTimeouts() +
          F(" protoErr=") + sessionProbe_.apContinuousProtocolErrors() + F(" stale=") +
          sessionProbe_.apContinuousStalePackets() + F(" postComplete=") +
          sessionProbe_.apContinuousPostCompletePackets() + F(" trackCancel=") +
          sessionProbe_.apContinuousTrackChangeCancels() + F(" channel=") +
          sessionProbe_.apContinuousChannelId() + F(" offset=") +
          sessionProbe_.apContinuousCurrentOffsetBytes() + F(" rangeBytes=") +
          sessionProbe_.apContinuousRangeBytes() + F(" responsePackets=") +
          sessionProbe_.apContinuousResponsePackets() + F(" lastCmd=0x") +
          String(sessionProbe_.apContinuousLastCommand(), HEX) + F(" failureCode=") +
          sessionProbe_.apContinuousFailureCode() + F(" format=") +
          sessionProbe_.apContinuousCandidateFormat());
    n.add(String(F("AP Continuous ring storage=")) + sessionProbe_.apContinuousRingStorage() +
          F(" cap=") + sessionProbe_.apContinuousRingCapacityBytes() + F(" highWater=") +
          sessionProbe_.apContinuousRingHighWaterBytes() + F(" buffered=") +
          sessionProbe_.apContinuousRingBufferedBytes() + F(" produced=") +
          sessionProbe_.apContinuousRingProducedBytes() + F(" consumed=") +
          sessionProbe_.apContinuousRingConsumedBytes() + F(" backpressure=") +
          sessionProbe_.apContinuousRingBackpressure() + F(" gap=") +
          sessionProbe_.apContinuousRingGapErrors() + F(" duplicate=") +
          sessionProbe_.apContinuousRingDuplicateErrors() + F(" producerErr=") +
          sessionProbe_.apContinuousRingProducerErrors() + F(" valid=") +
          (sessionProbe_.apContinuousRingValid() ? F("yes") : F("no")));
    n.add(String(F("AP Continuous integrity producerHash=0x")) +
          String(sessionProbe_.apContinuousProducerHash(), HEX) + F(" consumerHash=0x") +
          String(sessionProbe_.apContinuousConsumerHash(), HEX) + F(" match=") +
          (sessionProbe_.apContinuousHashMatch() ? F("yes") : F("no")) + F(" eof=") +
          (sessionProbe_.apContinuousEof() ? F("yes") : F("no")) + F(" reads=") +
          sessionProbe_.apContinuousConsumerReads() + F(" consumer=diagnostic decrypt=closed keyGate=") +
          (sessionProbe_.apStreamLiveKeyGateEligible() ? F("eligible") : F("blocked")));
    n.add(String(F("AP Continuous lastError=")) + sessionProbe_.apContinuousLastError());
    n.add(String(F("SPIRC remote ident=")) + (sessionProbe_.spircRemoteIdent()[0] ? sessionProbe_.spircRemoteIdent() : "none") +
          F(" name=") + (sessionProbe_.spircRemoteName()[0] ? sessionProbe_.spircRemoteName() : "none"));
    n.add(String(F("AP task attempts=")) + sessionProbe_.attempts() +
          F(" heap=") + sessionProbe_.heapBefore() + F("->") + sessionProbe_.heapAfter() +
          F(" minHeap=") + sessionProbe_.minHeapSeen() + F(" stackMin=") + sessionProbe_.stackMinFree());
    n.add(F("scope=live AP encrypted canary retained transiently behind MediaChunkSource; hard key gate + live decrypt/decoder consumer remain closed"));
    n.add(F("scope=dev.2n-r17 bounded 64KiB encrypted AP transport after frozen canary; consumer=diagnostic decrypt=closed; r16 AudioKey probe retained"));

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
    const auto vt = vorbisFixture_.telemetry();
    a.add(String(F("vorbisFixture=")) + (vorbisFixture_.active() ? F("active") : F("idle")) +
          F(" state=") + vorbisFixture_.lastError() + F(" starts=") + vt.starts + F(" complete=") + vt.completed +
          F(" result=") + vt.lastResult + F(" decodeCalls=") + vt.decodeCalls + F(" errors=") + vt.decodeErrors +
          F(" eos=") + vt.eosReports + F(" eof=") + vt.eofCompletions);
    a.add(String(F("vorbis PCM ")) + vt.sampleRate + F("Hz ch=") + vt.channels +
          F(" input=") + vt.inputConsumed + F("/") + vorbisFixture_.fixtureBytes() +
          F(" frames=") + vt.pcmFrames + F(" expected=") + vorbisFixture_.expectedFrames() +
          F(" feedCalls=") + vt.feedCalls + F(" feedFail=") + vt.feedFailures);
    const char* vorbisMode = vt.inputMode == SpotifyVorbisFixturePlayer::InputMode::AesChunked ? "aes-chunked" :
                             (vt.inputMode == SpotifyVorbisFixturePlayer::InputMode::Chunked ? "chunked" : "contiguous");
    a.add(String(F("vorbis stream mode=")) + vorbisMode +
          F(" source=") + vorbisFixture_.sourceName() + F(" contract=MediaChunkSource") +
          F(" chunk=") + vt.chunkBytes + F(" chunks=") + vt.chunkLoads + F(" supplied=") + vt.sourceSupplied +
          F(" stageHigh=") + vt.stagingHighWater + F("/") + vt.stagingBytes + F(" refills=") + vt.refillWaits);
    a.add(String(F("vorbis crypto mode=")) + (vt.inputMode == SpotifyVorbisFixturePlayer::InputMode::AesChunked ? F("aes128-ctr") : F("none")) +
          F(" keyBytes=") + vt.decryptKeyBytes + F(" fixedIv=") + (vt.fixedIv ? F("yes") : F("no")) +
          F(" decryptCalls=") + vt.decryptCalls + F(" decryptBytes=") + vt.decryptBytes +
          F(" decryptMax=") + vt.maxDecryptUs + F("us"));
    a.add(String(F("vorbis timing decodeMax=")) + vt.maxDecodeUs + F("us feedMax=") + vt.maxFeedUs +
          F("us stackMin=") + vt.stackMinFree + F(" outBuf=") + vt.outputBufferBytes);
    a.add(String(F("vorbis memory internal=")) + vt.internalHeapBefore + F("->") + vt.internalHeapAfter +
          F(" min=") + vt.internalHeapMin + F(" psram=") + vt.psramBefore + F("->") + vt.psramAfter);
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
    if (old && !enabled_) { vorbisFixture_.stop(); pcmTest_.stop(); audio_.stopTestTone(); audio_.end(); ready_=false; if (sessionProbe_.active()) sessionProbe_.requestStop(); else sessionProbe_.reset(); }
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
