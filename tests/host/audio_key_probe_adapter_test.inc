// Included after actual class declaration and real adapter/UI definitions.
// Platform, HTTP request and JSON sinks are host substitutes; no network is used.
static void setReport(SpotifySessionProbe& s, uint8_t seed, uint32_t gen) {
  auto& r=s.metadataAudit_;
  spotify_metadata_audit::reset(r);
  r.status=spotify_metadata_audit::Status::Ok;
  r.countryValid=r.catalogueValid=r.hasRequestedGid=true;
  std::memcpy(r.country,"NG",3); std::memcpy(r.catalogue,"premium",8);
  for(unsigned j=0;j<3;++j) {
    auto& t=j?r.alternatives[j-1]:r.primary;
    t.hasGid=true; t.territory=spotify_metadata_audit::Territory::Allowed;
    for(unsigned i=0;i<16;++i)t.gid[i]=static_cast<uint8_t>(seed+j*20+i);
    t.filesSeen=t.filesStored=1; t.files[0].format=1;
    for(unsigned i=0;i<20;++i)t.files[0].id[i]=static_cast<uint8_t>(seed+j*30+i);
  }
  std::memcpy(r.requestedGid,r.primary.gid,16);
  std::memcpy(s.selectedTrackGid_,r.primary.gid,16);
  r.alternativesSeen=r.alternativesStored=2;
  s.metadataAuditGeneration_=gen;
}
static void ready(SpotifySessionProbe& s) {
  s.state_=SpotifySessionProbe::State::SpircReady;
  s.stopRequested_=false; s.audioKeyPending_=false;
  s.metadataMercurySequence_=~uint64_t(0);
  s.apStreamPending_=false; s.apStreamCompletedProbes_=3;
  s.apStreamLiveVerifySuccesses_=1;
  spotify_key_probe::Request unused{};
  (void)s.takeKeyProbeRequest(unused,false);
}
static std::vector<uint8_t> response(uint32_t seq) {
  std::vector<uint8_t> v(20,0x5Au);
  v[0]=static_cast<uint8_t>(seq>>24); v[1]=static_cast<uint8_t>(seq>>16);
  v[2]=static_cast<uint8_t>(seq>>8); v[3]=static_cast<uint8_t>(seq);
  return v;
}
static void assertNormalUnchanged(const SpotifySessionProbe& s) {
  assert(s.audioKeyRequests_==4 && s.audioKeyResponses_==4 && s.audioKeySuccesses_==0);
  assert(s.audioKeyStaleResponses_==0 && s.audioKeyBytes_==0 && s.mediaKeyServiceBlocked_);
  for(auto b:s.audioKey_)assert(b==0x44u);
  assert(!s.apStreamLiveKeyGateEligible());

}
int main() {
  using namespace spotify_key_probe;
  UiSmoke ui; auto& s=ui.sessionProbe_;
  s.openKeyProbeSession(); setReport(s,1,7); ready(s);
  s.audioKeyRequests_=s.audioKeyResponses_=s.audioKeyNextSequence_=4;
  s.mediaKeyServiceBlocked_=true; std::memset(s.audioKey_,0x44,sizeof(s.audioKey_));
  // GET even with query action=start is read-only, and HTML is never cached.
  AsyncWebServerRequest get; get.query["action"].text="start";
  ui.handleKeyProbeGet(&get);
  assert(get.status==200 && get.headers["Cache-Control"]=="no-store");
  assert(get.body.find("name='generation' value='7'")!=std::string::npos);
  assert(!s.keyProbe_.busy());
  // Query parameters are not accepted as a POST body.
  AsyncWebServerRequest bad; bad.query["action"].text="start"; bad.query["generation"].text="7";
  ui.handleKeyProbePost(&bad); assert(bad.status==400 && !s.keyProbe_.busy());
  for(const char* value:{"0","-1","7x","4294967296"}) {
    AsyncWebServerRequest req; req.post["action"].text="start"; req.post["generation"].text=value;
    ui.handleKeyProbePost(&req); assert(req.status==400);
  }
  // Stale page cannot run against a newly selected track.
  AsyncWebServerRequest stale; stale.post["action"].text="start"; stale.post["generation"].text="6";
  stale.post["gid"].text="0102030405060708090a0b0c0d0e0f10";
  ui.handleKeyProbePost(&stale); assert(stale.status==409);
  stale.post["generation"].text="7"; stale.post["gid"].text="ffffffffffffffffffffffffffffffff";
  ui.handleKeyProbePost(&stale); assert(stale.status==409); // same generation, old boot/other GID
  AsyncWebServerRequest start; start.post["action"].text="start"; start.post["generation"].text="7"; start.post["gid"].text="0102030405060708090a0b0c0d0e0f10";
  ui.handleKeyProbePost(&start); assert(start.status==202);
  ui.handleKeyProbePost(&start); assert(start.status==409); // duplicate POST is idempotent
  assertNormalUnchanged(s);
  Request q{}; nowMs=100u;
  assert(s.takeKeyProbeRequest(q,true)); assert(q.sequence==4u && s.audioKeyNextSequence_==5u);
  s.finishKeyProbeWrite(q.sequence,true);
  auto v=response(q.sequence); nowMs=200u;
  assert(s.consumeKeyProbeResponse(0x0Du,v));
  assert(std::all_of(v.begin(),v.end(),[](uint8_t b){return b==0u;}));
  assertNormalUnchanged(s);  // no key stored, latch and normal counters untouched
  auto normal=response(3); const auto original=normal;
  assert(!s.consumeKeyProbeResponse(0x0Du,normal)); assert(normal==original);
  nowMs=700u; assert(s.takeKeyProbeRequest(q,true));
  assert(q.target.alternative==0);
  // Changing metadata cancels diagnostics, but quarantines the in-flight key.
  s.clearMetadataAudit(spotify_metadata_audit::Status::Pending,nullptr);
  auto late=response(q.sequence); assert(s.consumeKeyProbeResponse(0x0Du,late));
  assert(std::all_of(late.begin(),late.end(),[](uint8_t b){return b==0u;}));
  Summary info{}; uint32_t gen; bool isReady;
  s.keyProbeSummary(info,gen,isReady);
  assert(info.state==State::Cancelled && info.accepted==1 && info.lateResponses==1 && !isReady);
  assertNormalUnchanged(s);
  // A reset/reconnect cannot replenish the per-boot limits or repeat a tested GID.
  s.clearMetadataAudit(spotify_metadata_audit::Status::Idle,nullptr,true);
  s.closeKeyProbeSession(Reason::SessionClosed); s.openKeyProbeSession();
  setReport(s,1,20); ready(s); nowMs=30000u;
  assert(s.requestKeyProbe(20,s.metadataAudit_.requestedGid)==Reason::AlreadyTested);
  setReport(s,2,21); ready(s);
  assert(s.requestKeyProbe(21,s.metadataAudit_.requestedGid)==Reason::None); assert(s.takeKeyProbeRequest(q,true));
  assert(q.sequence==6u && s.audioKeyNextSequence_==7u); // shared allocator continues
  AsyncWebServerRequest cancel; cancel.post["action"].text="cancel";
  ui.handleKeyProbePost(&cancel); assert(cancel.status==202);
  auto secret=response(q.sequence); assert(s.consumeKeyProbeResponse(0x0Du,secret));
  assert(std::all_of(secret.begin(),secret.end(),[](uint8_t b){return b==0u;}));
  assertNormalUnchanged(s);
  // Do not let diagnostics wrap the normal allocator.
  setReport(s,3,22); s.audioKeyNextSequence_=UINT32_MAX; ready(s);
  nowMs=50000u; assert(s.requestKeyProbe(22,s.metadataAudit_.requestedGid)==Reason::NotReady);
  // UI contains only typed diagnostic evidence and no material from the key.
  JsonArray lines; ui.appendMetadataAudit(lines); ui.appendKeyProbeAudit(lines);
  assert(lines.text.find("AudioKey probe state=")!=std::string::npos);
  assert(lines.text.find("keyStorage=none consumer=closed")!=std::string::npos);
  assert(lines.text.find("5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a")==std::string::npos);
  assert(hostCriticalDepth==0);
  std::cout<<"r16 actual AP adapter + HTTP/JSON helpers PASS (stubbed platform, not target firmware)\n";
  std::cout<<"Checked POST-only activation, strict generation, replay, tuple/seq ownership, key wipe, late replies, normal-state isolation, reset budget, no String allocation under mux\n";
}
