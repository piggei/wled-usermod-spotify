#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
// Simulate the previously troublesome SDK macros at the public header boundary.
#define IV_BYTES (16)
#define KEY_BYTES (32)
#define HEX 16
#include "spotify/SpotifyMetadataAudit.h"

namespace ma = spotify_metadata_audit;
using Bytes = std::vector<uint8_t>;
static unsigned int cases = 0u;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__func__) + ": " + #x); } while (false)
void putVarint(Bytes& out, uint64_t value) {
  do { const uint8_t part = value & 127u; value >>= 7u; out.push_back(part | (value ? 128u : 0u)); } while (value);
}
Bytes field(uint32_t number, const Bytes& bytes) {
  Bytes out; putVarint(out, (static_cast<uint64_t>(number) << 3u) | 2u);
  putVarint(out, bytes.size()); out.insert(out.end(), bytes.begin(), bytes.end()); return out;
}
Bytes scalar(uint32_t number, uint64_t value) {
  Bytes out; putVarint(out, static_cast<uint64_t>(number) << 3u); putVarint(out, value); return out;
}
Bytes operator+(Bytes a, const Bytes& b) { a.insert(a.end(), b.begin(), b.end()); return a; }
Bytes text(const std::string& s) { return Bytes(s.begin(), s.end()); }
const Bytes gidA(16u, 0x11u), gidB(16u, 0x22u), fileA(20u, 0x33u), fileB(20u, 0x44u);
Bytes file(const Bytes& id, uint32_t format=1u) { return field(12u, field(1u, id) + scalar(2u, format)); }
Bytes track(const Bytes& id, const Bytes& fid) { return field(1u, id) + field(2u, text("fixture")) + file(fid); }
Bytes rule(const std::string& list, bool allow=true, const std::string& cat="premium") {
  return field(11u, field(5u, text(cat)) + field(allow ? 2u : 3u, text(list)));
}
struct Parser {
  ma::Report& report;
  bool operator()(const Bytes& b, const char* country="NG", const char* catalogue="premium") const {
    return ma::parse(b.data(), b.size(), gidA.data(), country, catalogue, report);
  }
};
void run() {
  std::unique_ptr<ma::Report> ptr(new ma::Report);
  ma::Report& r=*ptr;
  Parser parse{r};
  auto caseDone=[](){ ++cases; };

  CHECK(parse(track(gidA,fileA)+rule("NGIT")));
  CHECK(r.status==ma::Status::Ok && r.primary.territory==ma::Territory::Allowed);
  CHECK(r.primary.applicable==1u && r.primary.filesSeen==1u);
  CHECK(ma::comparePrimaryPair(r,gidA.data(),fileA.data())==ma::Pair::Match);
  caseDone();

  const Bytes relink=track(gidA,fileA)+rule("ITUS")+
      field(13u,track(gidB,fileB)+rule("NG"));
  CHECK(parse(relink)); CHECK(r.primary.territory==ma::Territory::Restricted);
  CHECK(r.alternativesStored==1u && r.alternativesSeen==1u);
  CHECK(r.alternatives[0].territory==ma::Territory::Allowed);
  CHECK(std::memcmp(r.alternatives[0].files[0].id,fileB.data(),20u)==0);
  CHECK(ma::comparePrimaryPair(r,gidA.data(),fileB.data())==ma::Pair::Mismatch);
  caseDone();

  CHECK(parse(track(gidB,fileA)+rule("NG")));
  CHECK(std::memcmp(r.requestedGid,r.primary.gid,16u)!=0);
  CHECK(ma::comparePrimaryPair(r,gidA.data(),fileA.data())==ma::Pair::Mismatch);
  CHECK(ma::comparePrimaryPair(r,gidB.data(),fileA.data())==ma::Pair::Match);
  caseDone();

  CHECK(parse(field(2u,text("no gid"))+file(fileA)));
  CHECK(!r.primary.hasGid && r.primary.territory==ma::Territory::Unknown);
  CHECK(ma::comparePrimaryPair(r,gidA.data(),fileA.data())==ma::Pair::Unknown);
  caseDone();

  CHECK(parse(track(gidA,fileA)+field(3u,field(1u,gidB))+
             field(4u,field(1u,gidB))));
  CHECK(std::memcmp(r.primary.gid,gidA.data(),16u)==0); caseDone();

  CHECK(parse(track(gidA,fileA)+rule("ANGB")));
  // N/G is an unaligned substring spanning the country codes AN and GB.
  CHECK(r.primary.territory==ma::Territory::Restricted); caseDone();
  CHECK(parse(track(gidA,fileA)+rule("NG",false)));
  CHECK(r.primary.territory==ma::Territory::Restricted); caseDone();
  CHECK(parse(track(gidA,fileA)+rule("IT",false)));
  CHECK(r.primary.territory==ma::Territory::Allowed); caseDone();
  CHECK(parse(track(gidA,fileA)+rule("")));
  CHECK(r.primary.territory==ma::Territory::Restricted); caseDone();
  CHECK(parse(track(gidA,fileA)+rule("",false)));
  CHECK(r.primary.territory==ma::Territory::Allowed); caseDone();
  CHECK(parse(track(gidA,fileA)+rule("NGA")));
  CHECK(r.primary.territory==ma::Territory::Unknown && r.primary.unknownRules==1u); caseDone();
  CHECK(parse(track(gidA,fileA)+rule("ng")));
  CHECK(r.primary.territory==ma::Territory::Unknown); caseDone();
  CHECK(parse(track(gidA,fileA)+rule("NG"),"", "premium"));
  CHECK(r.primary.territory==ma::Territory::Unknown); caseDone();
  CHECK(parse(track(gidA,fileA)+rule("NG"),"NG", "none"));
  CHECK(r.primary.territory==ma::Territory::Unknown); caseDone();
  CHECK(parse(track(gidA,fileA)+rule("NG",true,"free")));
  CHECK(r.primary.ignoredRules==1u && r.primary.territory==ma::Territory::Unknown); caseDone();
  CHECK(parse(track(gidA,fileA)+rule("NG",false,"free")+rule("NG")));
  CHECK(r.primary.territory==ma::Territory::Allowed); caseDone();

  CHECK(parse(track(gidA,fileA)+field(11u,scalar(1u,1u)+field(2u,text("NG")))));
  CHECK(r.primary.territory==ma::Territory::Allowed); caseDone();
  CHECK(parse(track(gidA,fileA)+field(11u,field(1u,Bytes{0u,1u})+field(2u,text("NG")))));
  CHECK(r.primary.territory==ma::Territory::Allowed); caseDone();
  CHECK(parse(track(gidA,fileA)+field(11u,scalar(1u,2u)+field(2u,text("NG")))));
  CHECK(r.primary.territory==ma::Territory::Allowed); caseDone();
  CHECK(parse(track(gidA,fileA)+field(11u,scalar(1u,99u)+field(2u,text("NG")))));
  CHECK(r.primary.territory==ma::Territory::Unknown); caseDone();
  CHECK(parse(track(gidA,fileA)+field(11u,field(2u,text("NG")))));
  CHECK(r.primary.territory==ma::Territory::Unknown); caseDone();
  CHECK(parse(track(gidA,fileA)+field(11u,scalar(1u,0u)+field(5u,text("premium"))+field(2u,text("NG")))));
  CHECK(r.primary.territory==ma::Territory::Unknown); caseDone();
  CHECK(parse(track(gidA,fileA)+field(11u,field(5u,text("premium"))+scalar(4u,3u)+field(2u,text("NG")))));
  CHECK(r.primary.territory==ma::Territory::Unknown); caseDone();
  CHECK(parse(track(gidA,fileA)+field(11u,field(5u,text("premium"))+field(2u,text("NG"))+field(3u,text("IT")))));
  CHECK(r.primary.territory==ma::Territory::Unknown); caseDone();
  CHECK(parse(track(gidA,fileA)+rule("NG")+rule("IT")));
  CHECK(r.primary.territory==ma::Territory::Unknown); caseDone();
  CHECK(parse(track(gidA,fileA)+field(11u,field(5u,text("premium"))+field(2u,text("NG"))+scalar(9u,1u))));
  CHECK(r.primary.territory==ma::Territory::Unknown); caseDone();
  CHECK(parse(track(gidA,fileA)+rule(std::string(1026u,'N'))));
  CHECK(r.primary.territory==ma::Territory::Unknown); caseDone();

  CHECK(parse(track(gidA,fileA)+rule("NG")+field(19u,Bytes{})+field(14u,Bytes{})+scalar(17u,123u)));
  CHECK(r.primary.availability==1u && r.primary.salePeriods==1u && r.primary.earliestLive);
  ma::TrackView tv{}; ma::view(r.primary,tv);
  CHECK(tv.availability==1u && tv.salePeriods==1u && tv.earliestLive); caseDone();

  CHECK(parse(track(gidA,fileA)+field(13u,field(1u,gidB))));
  CHECK(r.alternativesStored==1u && r.alternatives[0].hasGid && !r.alternatives[0].filesStored); caseDone();
  Bytes many=track(gidA,fileA);
  for(size_t i=0;i<11u;++i) many=many+field(13u,track(gidB,fileB));
  CHECK(parse(many)); CHECK(r.alternativesSeen==11u && r.alternativesStored==8u);
  CHECK(r.alternativesTruncated && r.status==ma::Status::Limited); caseDone();
  CHECK(parse(track(gidA,fileA)+field(13u,track(gidB,fileB)+field(13u,track(gidA,fileA)))));
  CHECK(r.alternativesSeen==1u && r.alternatives[0].nestedAlternatives==1u);
  CHECK(r.status==ma::Status::Limited); caseDone();

  CHECK(parse(field(1u,gidA)+file(fileA,2u)+file(fileB,1u)));
  ma::view(r.primary,tv); CHECK(tv.preferredFormat==1 && std::memcmp(tv.preferredFile,fileB.data(),20u)==0); caseDone();
  many=track(gidA,fileA);
  for(size_t i=0;i<10u;++i) many=many+file(fileB,0u);
  CHECK(parse(many)); CHECK(r.primary.filesStored==8u && r.primary.filesSeen==11u && r.primary.filesTruncated);
  const Bytes other(20u,0x99u);
  CHECK(ma::comparePrimaryPair(r,gidA.data(),other.data())==ma::Pair::Unknown); caseDone();
  CHECK(parse(field(1u,gidA)+file(Bytes(19u,0u))));
  CHECK(r.primary.invalidFiles==1u && !r.primary.filesStored && r.status==ma::Status::Limited); caseDone();
  CHECK(parse(track(gidA,fileA)+field(1u,gidB)));
  CHECK(r.primary.gidConflict && ma::comparePrimaryPair(r,gidB.data(),fileA.data())==ma::Pair::Unknown); caseDone();
  CHECK(parse(track(gidA,fileA)+field(1u,gidA)));
  CHECK(!r.primary.gidConflict); caseDone();

  CHECK(!parse(field(1u,Bytes(15u,0u)))); CHECK(r.status==ma::Status::Malformed); caseDone();
  CHECK(!parse(Bytes{0u})); CHECK(r.status==ma::Status::Malformed); caseDone();
  CHECK(!parse(Bytes{0x0bu})); CHECK(r.status==ma::Status::Malformed); caseDone();
  CHECK(!parse(Bytes(11u,0x80u))); CHECK(r.status==ma::Status::Malformed); caseDone();
  CHECK(!parse(Bytes{0x0au,0x20u,0x00u})); CHECK(r.status==ma::Status::Malformed); caseDone();
  CHECK(!parse(Bytes{0x0au,0xffu,0xffu,0xffu,0xffu,0xffu,0xffu,0xffu,0xffu,0xffu,0x02u})); caseDone();
  CHECK(!parse(track(gidA,fileA)+rule("NG")+Bytes{0x0au,0x20u}));
  CHECK(r.primary.territory==ma::Territory::Unknown);
  CHECK(ma::comparePrimaryPair(r,gidA.data(),fileA.data())==ma::Pair::Unknown); caseDone();
  CHECK(!parse(Bytes(65537u,0u))); CHECK(r.status==ma::Status::TooLarge); caseDone();
  many=track(gidA,fileA);
  for(size_t i=0;i<4100u;++i) many=many+scalar(40u,0u);
  CHECK(!parse(many)); CHECK(r.status==ma::Status::Budget); caseDone();
  CHECK(!ma::parse(nullptr,4u,gidA.data(),"NG","premium",r)); caseDone();

  char out[41]; ma::hexId(fileA.data(),20u,out,sizeof(out)); CHECK(std::string(out)==std::string(40u,'3'));
  ma::hexId(fileA.data(),20u,out,4u); CHECK(out[0]=='\0'); caseDone();
  ma::reset(r); CHECK(r.status==ma::Status::Idle && r.alternativesStored==0u); caseDone();

  std::mt19937 random(0x4f474731u);
  const Bytes valid=relink;
  for(size_t trial=0;trial<12000u;++trial) {
    Bytes data;
    if ((trial%3u)==0u) {
      const size_t len=random()%768u; data.resize(len);
      for(auto& c:data) c=static_cast<uint8_t>(random());
    } else {
      data=valid;
      if ((trial%3u)==1u) data.resize(random()%(valid.size()+1u));
      else for(size_t i=0;i<1u+random()%4u;++i) data[random()%data.size()]^=static_cast<uint8_t>(random());
    }
    parse(data);
    CHECK(r.alternativesStored<=ma::kMaxAlternatives && r.primary.filesStored<=ma::kMaxFiles);
    CHECK(r.fieldsRead<=ma::kMaxFields+1u);
    for(size_t i=0;i<r.alternativesStored;++i) CHECK(r.alternatives[i].filesStored<=ma::kMaxFiles);
  }
  std::cout << "metadata audit native tests PASS cases=" << cases << " fuzz=12000"
            << " reportBytes=" << sizeof(ma::Report) << " summaryBytes=" << sizeof(ma::Summary)
            << " trackViewBytes=" << sizeof(ma::TrackView) << "\n";
}
int main() {
  try { run(); return 0; }
  catch (const std::exception& ex) { std::cerr << ex.what() << "\n"; return 1; }
}
