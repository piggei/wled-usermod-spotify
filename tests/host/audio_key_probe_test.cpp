#include "spotify/SpotifyAudioKeyProbe.h"
#include <cassert>
#include <cstring>
#include <functional>
#include <iostream>
#include <vector>

using namespace spotify_key_probe;
namespace ma = spotify_metadata_audit;
// Compiled from the exact unchanged firmware builder by the Python runner.
std::vector<uint8_t> buildAudioKeyRequest(const uint8_t*, const uint8_t*, uint32_t);
static unsigned cases = 0u;
static void test(const char* name, const std::function<void()>& f) {
  f(); ++cases; std::cout << "PASS " << name << '\n';
}
static ma::Track track(uint8_t seed) {
  ma::Track t{};
  t.hasGid = true; t.territory = ma::Territory::Allowed;
  for (size_t i = 0u; i < ma::kGidBytes; ++i) t.gid[i] = static_cast<uint8_t>(seed + i);
  const int formats[] = {1, 2, 0, 8};
  t.filesSeen = t.filesStored = 4u;
  for (unsigned j = 0u; j < 4u; ++j) {
    t.files[j].format = formats[j];
    for (size_t i = 0u; i < ma::kFileIdBytes; ++i)
      t.files[j].id[i] = static_cast<uint8_t>(seed + i + j * 37u);
  }
  return t;
}
static ma::Report report(uint8_t alternatives = 2u, uint8_t seed = 1u) {
  ma::Report r{}; r.status = ma::Status::Ok;
  r.countryValid = r.catalogueValid = r.hasRequestedGid = true;
  std::memcpy(r.country, "NG", 3u); std::memcpy(r.catalogue, "premium", 8u);
  r.primary = track(seed);
  std::memcpy(r.requestedGid, r.primary.gid, ma::kGidBytes);
  r.alternativesSeen = r.alternativesStored = alternatives;
  for (uint8_t i = 0u; i < alternatives; ++i) r.alternatives[i] = track(seed + 10u + i);
  return r;
}
static Summary summary(const Probe& p, uint32_t now = 20000u) {
  Summary s{}; p.summary(now, s); return s;
}
static Result result(const Probe& p, uint8_t i) {
  Result r{}; assert(p.result(summary(p).run, i, r)); return r;
}
static std::vector<uint8_t> reply(uint32_t seq, bool accepted) {
  std::vector<uint8_t> v(accepted ? 20u : 6u, 0xA5u);
  v[0] = static_cast<uint8_t>(seq >> 24u); v[1] = static_cast<uint8_t>(seq >> 16u);
  v[2] = static_cast<uint8_t>(seq >> 8u); v[3] = static_cast<uint8_t>(seq);
  if (!accepted) { v[4] = 0u; v[5] = 1u; }
  return v;
}
static void sendReply(Probe& p, const Request& req, bool accepted, uint32_t now,
                      uint32_t gen = 1u, uint32_t epoch = 1u) {
  auto v = reply(req.sequence, accepted);
  assert(p.consume(accepted ? 0x0Du : 0x0Eu, v.data(), v.size(), gen, epoch, now));
}
static bool take(Probe& p, uint32_t now, uint32_t gen, uint32_t epoch, bool ready,
                 Request& req, bool maySend=true) {
  // Model the AP's shared allocator; ordinary requests may also advance it.
  return p.takeRequest(now,gen,epoch,ready,4u+summary(p).requestsUsed,req,maySend);
}
static void complete(Probe& p, uint32_t now = 100u, uint32_t gen = 1u, uint32_t epoch = 1u) {
  for (unsigned i = 0u; i < kMaxTargets && p.busy(); ++i) {
    Request req{};
    assert(take(p,now, gen, epoch, true, req));
    sendReply(p, req, false, now + 100u, gen, epoch);
    now += 1000u;
  }
  assert(summary(p).state == State::Complete);
}
int main() {
  test("boot is idle and consumes no budget", [] {
    Probe p; auto s = summary(p); assert(s.state == State::Idle && !p.pending());
    assert(!s.run && !s.targets && !s.requestsUsed && !s.runsUsed);
  });
  test("primary plus two alternatives, same format, correct tuple ownership", [] {
    Probe p; auto r = report(); assert(p.enqueue(r, 1, 1, 1, 0, true) == Reason::None);
    assert(summary(p).targets == 3u && summary(p).sent == 0u);
    for (uint8_t i = 0; i < 3; ++i) {
      auto item = result(p, i); const auto& expected = i ? r.alternatives[i-1] : r.primary;
      assert(item.target.alternative == static_cast<int>(i) - 1 && item.target.format == 1);
      assert(!std::memcmp(item.target.gid, expected.gid, 16));
      assert(!std::memcmp(item.target.file, expected.files[0].id, 20));
    }
  });
  test("no alternatives is a primary-only experiment", [] {
    Probe p; assert(p.enqueue(report(0), 1, 1, 1, 0, true) == Reason::None);
    complete(p); assert(summary(p).sent == 1 && summary(p).rejected == 1);
  });
  test("eight alternatives bounded to two, rest explicitly limited", [] {
    Probe p; assert(p.enqueue(report(8), 1, 1, 1, 0, true) == Reason::None);
    assert(summary(p).targets == 3 && summary(p).limitedAlternatives == 6);
  });
  test("stale posted generation never queues", [] {
    Probe p; assert(p.enqueue(report(), 2, 1, 1, 0, true) == Reason::StaleGeneration);
    assert(!summary(p).runsUsed);
  });
  test("zero generation never queues", [] {
    Probe p; assert(p.enqueue(report(), 0, 0, 1, 0, true) == Reason::StaleGeneration);
  });
  test("transport must finish normal key/metadata/canary work", [] {
    Probe p; assert(p.enqueue(report(), 1, 1, 1, 0, false) == Reason::NotReady);
    assert(p.enqueue(report(), 1, 1, 0, 0, true) == Reason::NotReady);
  });
  for (auto status : {ma::Status::Idle, ma::Status::Pending, ma::Status::Limited,
                      ma::Status::Malformed, ma::Status::HttpError, ma::Status::Budget}) {
    test("non-ok metadata rejected without budget", [status] {
      Probe p; auto r = report(); r.status = status;
      assert(p.enqueue(r, 1, 1, 1, 0, true) == Reason::MetadataNotOk);
      assert(!summary(p).runsUsed);
    });
  }
  test("country and catalogue must be valid", [] {
    Probe p; auto r = report(); r.countryValid = false;
    assert(p.enqueue(r, 1, 1, 1, 0, true) == Reason::MetadataNotOk);
    r.countryValid = true; r.catalogueValid = false;
    assert(p.enqueue(r, 1, 1, 1, 0, true) == Reason::MetadataNotOk);
  });
  test("mismatched requested/returned GID is not silently relinked", [] {
    Probe p; auto r = report(); r.primary.gid[0] ^= 0x80u;
    assert(p.enqueue(r, 1, 1, 1, 0, true) == Reason::PrimaryMismatch);
  });
  test("missing and conflicting primary GID rejected", [] {
    Probe p; auto r = report(); r.hasRequestedGid = false;
    assert(p.enqueue(r, 1, 1, 1, 0, true) == Reason::PrimaryMismatch);
    r.hasRequestedGid = true; r.primary.gidConflict = true;
    assert(p.enqueue(r, 1, 1, 1, 0, true) == Reason::PrimaryMismatch);
  });
  for (auto territory : {ma::Territory::Unknown, ma::Territory::Restricted}) {
    test("unverified/restricted primary cannot become authorized", [territory] {
      Probe p; auto r = report(); r.primary.territory = territory;
      assert(p.enqueue(r, 1, 1, 1, 0, true) == Reason::TerritoryUnverified);
    });
  }
  test("earliest-live presence is reported but not misread as authorization", [] {
    Probe p; auto r = report(); r.primary.earliestLive = true;
    assert(p.enqueue(r, 1, 1, 1, 0, true) == Reason::None);
    assert(result(p, 0).target.earliestLive);
  });
  test("truncated or invalid files rejected", [] {
    Probe p; auto r = report(); r.primary.filesTruncated = true;
    assert(p.enqueue(r, 1, 1, 1, 0, true) == Reason::InvalidFiles);
    r.primary.filesTruncated = false; r.primary.invalidFiles = 1;
    assert(p.enqueue(r, 1, 1, 1, 0, true) == Reason::InvalidFiles);
  });
  test("ambiguous same-format IDs rejected", [] {
    Probe p; auto r = report(); r.primary.files[1].format = 1;
    assert(p.enqueue(r, 1, 1, 1, 0, true) == Reason::InvalidFiles);
  });
  test("duplicate same-format same-ID is harmless", [] {
    Probe p; auto r = report(); r.primary.files[1] = r.primary.files[0];
    assert(p.enqueue(r, 1, 1, 1, 0, true) == Reason::None);
  });
  test("missing format 1 does not silently choose AAC or another bitrate", [] {
    Probe p; auto r = report(); r.primary.files[0].format = 99;
    assert(p.enqueue(r, 1, 1, 1, 0, true) == Reason::NoFormat);
  });
  test("zero ID rejected", [] {
    Probe p; auto r = report(); std::memset(r.primary.files[0].id, 0, 20);
    assert(p.enqueue(r, 1, 1, 1, 0, true) == Reason::InvalidFiles);
  });
  test("invalid alternatives skipped, valid pair retained", [] {
    Probe p; auto r = report(5);
    r.alternatives[0].territory = ma::Territory::Unknown;
    r.alternatives[1].territory = ma::Territory::Restricted;
    r.alternatives[2].hasGid = false; r.alternatives[3].files[0].format = 0;
    assert(p.enqueue(r, 1, 1, 1, 0, true) == Reason::None);
    assert(summary(p).skippedAlternatives == 4 && summary(p).targets == 2);
    assert(result(p, 1).target.alternative == 4);
  });
  test("duplicate primary/alternative pair skipped", [] {
    Probe p; auto r = report(); r.alternatives[0] = r.primary;
    assert(p.enqueue(r, 1, 1, 1, 0, true) == Reason::None);
    assert(summary(p).targets == 2 && summary(p).skippedAlternatives == 1);
  });
  test("queued POST replay cannot consume a second run", [] {
    Probe p; assert(p.enqueue(report(), 1, 1, 1, 0, true) == Reason::None);
    assert(p.enqueue(report(), 1, 1, 1, 0, true) == Reason::Busy);
    assert(summary(p).runsUsed == 1);
  });
  test("busy AP traffic delays send without delaying deadline checks", [] {
    Probe p; Request q{}; assert(p.enqueue(report(), 1, 1, 1, 0, true) == Reason::None);
    assert(!take(p,10, 1, 1, true, q, false) && !summary(p).sent);
    assert(take(p,20, 1, 1, true, q));
    assert(!take(p,2520, 1, 1, true, q, false));
    assert(summary(p).timeouts == 1 && summary(p).sent == 1);
  });
  test("frozen wire builder serializes diagnostic tuple and big-endian seq", [] {
    Probe p; Request q{}; assert(p.enqueue(report(), 1, 1, 1, 0, true) == Reason::None);
    assert(take(p,10, 1, 1, true, q)); auto w = buildAudioKeyRequest(q.target.file,q.target.gid,q.sequence);
    assert(w.size()==42 && !std::memcmp(w.data(),q.target.file,20) && !std::memcmp(w.data()+20,q.target.gid,16));
    assert(w[36]==0 && w[37]==0 && w[38]==0 && w[39]==4 && w[40]==0 && w[41]==0);
  });
  test("three explicit rejections complete the diagnostic, no retries", [] {
    Probe p; assert(p.enqueue(report(), 1, 1, 1, 0, true) == Reason::None); complete(p);
    auto s=summary(p); assert(s.sent==3 && s.responses==3 && s.rejected==3 && !s.accepted);
    Request q{}; assert(!take(p,10000,1,1,true,q));
    for (uint8_t i=0;i<3;++i) { auto r=result(p,i); assert(r.error0==0 && r.error1==1 && !r.keyBytes); }
  });
  test("alternative success records length only, never changes the other pairs", [] {
    Probe p; auto m=report(); Request q{}; assert(p.enqueue(m,1,1,1,0,true)==Reason::None);
    assert(take(p,10,1,1,true,q)); sendReply(p,q,false,20);
    assert(!take(p,519,1,1,true,q)); assert(take(p,520,1,1,true,q));
    sendReply(p,q,true,530); assert(take(p,1030,1,1,true,q)); sendReply(p,q,false,1040);
    auto s=summary(p); assert(s.accepted==1 && s.rejected==2 && s.state==State::Complete);
    auto r=result(p,1); assert(r.keyBytes==16 && r.command==0x0D && r.target.alternative==0);
    assert(!std::memcmp(r.target.gid,m.alternatives[0].gid,16));
  });
  test("duplicate and foreign sequence cannot claim another target", [] {
    Probe p; Request q{}; assert(p.enqueue(report(),1,1,1,0,true)==Reason::None);
    assert(take(p,10,1,1,true,q)); auto old=reply(q.sequence,true);
    sendReply(p,q,true,20);
    assert(p.consume(0x0D,old.data(),old.size(),1,1,30)); assert(summary(p).lateResponses==1);
    auto other=reply(3,true); assert(!p.consume(0x0D,other.data(),other.size(),1,1,40));
    assert(take(p,520,1,1,true,q));
    assert(p.consume(0x0D,old.data(),old.size(),1,1,530));
    assert(p.pending() && summary(p).accepted==1 && summary(p).lateResponses==2);
  });
  for (size_t n : {0u, 1u, 3u, 4u, 5u, 19u, 21u}) {
    test("malformed success is not key acceptance and aborts remaining work", [n] {
      Probe p; Request q{}; assert(p.enqueue(report(),1,1,1,0,true)==Reason::None);
      assert(take(p,10,1,1,true,q)); auto v=reply(q.sequence,true); v.resize(n,0);
      assert(p.consume(0x0D,v.data(),v.size(),1,1,20));
      auto s=summary(p); assert(s.state==State::Failed && s.protocolErrors==1 && !s.accepted);
      assert(result(p,1).outcome==Outcome::Cancelled);
    });
  }
  for (size_t n : {4u, 5u, 7u}) {
    test("malformed error reply aborts experiment", [n] {
      Probe p; Request q{}; assert(p.enqueue(report(),1,1,1,0,true)==Reason::None);
      assert(take(p,10,1,1,true,q)); auto v=reply(q.sequence,false); v.resize(n,0);
      assert(p.consume(0x0E,v.data(),v.size(),1,1,20)); assert(summary(p).protocolErrors==1);
    });
  }
  test("unrelated packet and malformed idle reply not swallowed", [] {
    Probe p; assert(!p.consume(0x0E,nullptr,0,1,1,0));
    assert(!p.consume(0xB5,nullptr,0,1,1,0));
  });
  test("timeout exactly at deadline; late success remains quarantined", [] {
    Probe p; Request q{}; assert(p.enqueue(report(),1,1,1,0,true)==Reason::None);
    assert(take(p,10,1,1,true,q)); sendReply(p,q,true,2510);
    assert(summary(p).timeouts==1 && !summary(p).accepted && summary(p).lateResponses==1);
  });
  test("time limit is checked at zero millis and wraps correctly", [] {
    Probe p; Request q{}; const uint32_t start=UINT32_MAX-100u;
    assert(p.enqueue(report(0),1,1,1,start,true)==Reason::None);
    assert(take(p,start+10u,1,1,true,q)); sendReply(p,q,true,start+200u);
    assert(summary(p).accepted==1 && result(p,0).rttMs==190u);
  });
  test("run deadline expires even when no request could be sent", [] {
    Probe p; Request q{}; assert(p.enqueue(report(),1,1,1,0,true)==Reason::None);
    assert(!take(p,kRunTimeoutMs,1,1,true,q,false));
    assert(summary(p).state==State::Failed && summary(p).reason==Reason::RunDeadline);
    assert(!summary(p).sent && summary(p).runsUsed==1);
  });
  test("track change cancels pending and planned rows", [] {
    Probe p; Request q{}; assert(p.enqueue(report(),1,1,1,0,true)==Reason::None);
    assert(take(p,10,1,1,true,q)); const auto old=q;
    assert(!take(p,20,2,1,true,q)); assert(summary(p).reason==Reason::TrackChanged);
    sendReply(p,old,true,30,2); assert(!summary(p).accepted && summary(p).lateResponses==1);
    for(uint8_t i=0;i<3;++i) assert(result(p,i).outcome==Outcome::Cancelled);
  });
  test("session change cancels before sending next tuple", [] {
    Probe p; Request q{}; assert(p.enqueue(report(),1,1,1,0,true)==Reason::None);
    assert(!take(p,10,1,2,true,q)); assert(summary(p).reason==Reason::SessionChanged);
  });
  test("reply received after epoch change is stale, not success", [] {
    Probe p; Request q{}; assert(p.enqueue(report(),1,1,1,0,true)==Reason::None);
    assert(take(p,10,1,1,true,q)); auto v=reply(q.sequence,true);
    assert(!p.consume(0x0D,v.data(),v.size(),1,2,20));
    assert(summary(p).state==State::Cancelled && !summary(p).accepted);
  });
  test("manual cancellation keeps a receipt for late replies", [] {
    Probe p; Request q{}; assert(p.enqueue(report(),1,1,1,0,true)==Reason::None);
    assert(take(p,10,1,1,true,q)); p.cancel(Reason::UserCancel); sendReply(p,q,true,20);
    assert(summary(p).reason==Reason::UserCancel && summary(p).lateResponses==1);
  });
  test("loss of readiness cancels queued work", [] {
    Probe p; Request q{}; assert(p.enqueue(report(),1,1,1,0,true)==Reason::None);
    assert(!take(p,10,1,1,false,q)); assert(summary(p).state==State::Cancelled);
  });
  test("write failure is terminal, no implicit retry or extra request", [] {
    Probe p; Request q{}; assert(p.enqueue(report(),1,1,1,0,true)==Reason::None);
    assert(take(p,10,1,1,true,q)); p.finishWrite(q.sequence,false,20);
    auto s=summary(p); assert(s.writeErrors==1 && s.state==State::Failed && s.requestsUsed==1);
    assert(!take(p,10000,1,1,true,q));
  });
  test("completed results survive track/session cancellation signals", [] {
    Probe p; assert(p.enqueue(report(0),1,1,1,0,true)==Reason::None); complete(p);
    p.cancel(Reason::TrackChanged); p.cancel(Reason::SessionClosed);
    assert(summary(p).state==State::Complete && summary(p).rejected==1);
  });
  test("same GID cannot be retried after generation/session changes", [] {
    Probe p; assert(p.enqueue(report(0),1,1,1,0,true)==Reason::None); complete(p);
    assert(p.enqueue(report(0),2,2,2,20000,true)==Reason::AlreadyTested);
  });
  test("new track cooldown is ten seconds, not a spin/retry loop", [] {
    Probe p; assert(p.enqueue(report(0),1,1,1,0,true)==Reason::None); complete(p);
    assert(p.enqueue(report(0,2),2,2,1,9999,true)==Reason::Cooldown);
    assert(p.enqueue(report(0,2),2,2,1,10000,true)==Reason::None);
  });
  test("at most four runs and twelve additional requests per boot", [] {
    Probe p;
    for(uint8_t i=0;i<4;++i) {
      const uint32_t t=i*20000u;
      assert(p.enqueue(report(2,i+1),i+1,i+1,i+1,t,true)==Reason::None);
      complete(p,t+100u,i+1,i+1);
    }
    assert(summary(p).runsUsed==4 && summary(p).requestsUsed==12);
    assert(p.enqueue(report(2,99),5,5,5,80000,true)==Reason::BootBudget);
  });
  test("cancellation does not refund a run budget", [] {
    Probe p;
    for(uint8_t i=0;i<4;++i) {
      assert(p.enqueue(report(0,i+1),i+1,i+1,1,i*20000u,true)==Reason::None);
      p.cancel(Reason::UserCancel);
    }
    assert(summary(p).runsUsed==4 && !summary(p).requestsUsed);
    assert(p.enqueue(report(0,99),5,5,1,80000,true)==Reason::BootBudget);
  });
  test("shared allocator may skip normal request sequences", [] {
    Probe p; Request q{}; assert(p.enqueue(report(),1,1,1,0,true)==Reason::None);
    assert(p.takeRequest(10,1,1,true,17u,q)); assert(q.sequence==17u); sendReply(p,q,false,20);
    assert(p.takeRequest(520,1,1,true,22u,q)); assert(q.sequence==22u);
  });
  test("same-epoch sequence reuse fails without sending", [] {
    Probe p; Request q{}; assert(p.enqueue(report(),1,1,1,0,true)==Reason::None);
    assert(p.takeRequest(10,1,1,true,4u,q)); sendReply(p,q,false,20);
    assert(!p.takeRequest(520,1,1,true,4u,q));
    assert(summary(p).reason==Reason::SequenceConflict && summary(p).sent==1);
  });
  test("old epoch receipts cannot capture a new normal key response", [] {
    Probe p; Request q{}; assert(p.enqueue(report(0),1,1,1,0,true)==Reason::None);
    assert(p.takeRequest(10,1,1,true,4u,q)); sendReply(p,q,false,20);
    auto v=reply(4,true); assert(!p.consume(0x0D,v.data(),v.size(),2,2,30));
    assert(summary(p).accepted==0);
    assert(p.enqueue(report(0,2),2,2,2,20000,true)==Reason::None);
    assert(p.takeRequest(20010,2,2,true,4u,q)); sendReply(p,q,true,20020,2,2);
    assert(summary(p).accepted==1);
  });
  test("snapshot result bounds and run identity checked", [] {
    Probe p; Result r{}; assert(!p.result(0,0,r));
    assert(p.enqueue(report(),1,1,1,0,true)==Reason::None);
    assert(!p.result(2,0,r) && !p.result(1,3,r) && p.result(1,2,r));
  });
  test("strict GID binding covers stale forms after a board reboot", [] {
    uint8_t gid[16];
    assert(parseGid("0102030405060708090a0b0c0d0e0f10",gid) && gid[0]==1 && gid[15]==16);
    assert(parseGid("0102030405060708090A0B0C0D0E0F10",gid));
    assert(!parseGid("0102030405060708090a0b0c0d0e0f1",gid));
    assert(!parseGid("0102030405060708090a0b0c0d0e0f100",gid));
    assert(!parseGid("0102030405060708090a0b0c0d0e0f1x",gid));
    assert(!parseGid("",gid) && !parseGid(nullptr,gid));
  });
  test("strict generation parsing", [] {
    uint32_t n=0;
    for(const char* bad : {"", "0", "-1", "+1", "1x", " 1", "1 ", "4294967296", "00000000001"})
      assert(!parseGeneration(bad,n));
    assert(!parseGeneration(nullptr,n));
    assert(parseGeneration("1",n) && n==1); assert(parseGeneration("4294967295",n) && n==UINT32_MAX);
  });
  test("synthetic interleavings preserve bounded counts and typed acceptance", [] {
    uint32_t rng=0x31415926u;
    for(unsigned trial=0;trial<200;++trial) {
      Probe p; uint32_t now=0,gen=1,epoch=1; Request last{};
      for(unsigned event=0;event<100;++event) {
        rng^=rng<<13u; rng^=rng>>17u; rng^=rng<<5u; now += rng%3001u;
        switch (rng%8u) {
          case 0: (void)p.enqueue(report(2,static_cast<uint8_t>(gen)),gen,gen,epoch,now,true); break;
          case 1: (void)take(p,now,gen,epoch,true,last); break;
          case 2: { auto v=reply(last.sequence,true); (void)p.consume(0x0D,v.data(),v.size(),gen,epoch,now); break; }
          case 3: { auto v=reply(last.sequence,false); (void)p.consume(0x0E,v.data(),v.size(),gen,epoch,now); break; }
          case 4: p.cancel(Reason::UserCancel); break;
          case 5: ++gen; (void)take(p,now,gen,epoch,true,last,false); break;
          case 6: ++epoch; (void)take(p,now,gen,epoch,false,last,false); break;
          case 7: (void)take(p,now,gen,epoch,true,last,false); break;
        }
        auto s=summary(p,now); assert(s.runsUsed<=4 && s.requestsUsed<=12 && s.targets<=3 && s.sent<=s.targets);
        assert(s.accepted+s.rejected+s.timeouts+s.protocolErrors+s.writeErrors<=s.targets);
        for(uint8_t i=0;i<s.targets;++i) {
          auto r=result(p,i);
          if(r.outcome==Outcome::Accepted) assert(r.keyBytes==16 && r.command==0x0D && r.sent);
          else assert(r.keyBytes==0);
        }
      }
    }
  });
  std::cout << "AudioKey probe native PASS: " << cases << " scenarios; 20000 deterministic interleaving events\n";
  std::cout << "Host sizes: Probe=" << sizeof(Probe) << " Summary=" << sizeof(Summary)
            << " Request=" << sizeof(Request) << " Result=" << sizeof(Result) << " bytes\n";
}
