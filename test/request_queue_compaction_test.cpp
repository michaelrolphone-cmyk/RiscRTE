// Durable-operation and finite-entry capacity model for RequestQueue. Exact
// ESP-IDF 4.4.7 page/GC behavior is qualified separately with the upstream NVS
// engine; this model uses its immutable 32-byte entries/4000-byte blob chunks
// and reserve-page ceiling, not an unbounded successful-write map.
#include "runtime/provisioning/RequestQueue.h"
#include "bootstrap/Json.h"
#include <openssl/sha.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
using namespace RiscProvision;
namespace {
bool sha(const void*p,uint32_t n,uint8_t*out){return SHA256(static_cast<const uint8_t*>(p),n,out)!=nullptr;}
std::string encode(JsonVariantConst value){std::string result;serializeJson(value,result);return result;}
std::string profile(unsigned count=125,char target='a'){
 JsonDocument d;d["schema"]="riscrte.provisioning";d["schema_version"]=4;
 d["wifi"]["ssid"]="test-owner-network";d["wifi"]["password"]="test-only-password";
 d["image"]["url"]="https://example.test/test-only-image.bin";d["image"]["bytes"]=0x550000;d["image"]["sha256"]=std::string(64,target);
 auto rows=d["files"].to<JsonArray>();for(unsigned i=0;i<count;++i){auto row=rows.add<JsonArray>();
  row.add(i==0?std::string("boot.json"):i==1?std::string("board.json"):i==2?std::string("default.elf"):"fixture-asset-"+std::to_string(i)+".json");
  row.add(i+1);row.add(std::string(64,'c'));}
 return encode(d.as<JsonVariantConst>());
}
struct PowerCut {};
struct Store {
 std::map<std::string,std::vector<uint8_t>> values;
 unsigned reads=0,sets=0,commits=0,erases=0,probes=0,event=0,cut=0,fail=0,failSet=0;
 bool after=false,corruptRead=false,wrongLength=false;size_t otherEntries=32,peak=0;
 std::vector<std::string> readKeys;
 static size_t entries(size_t n){return (n+31)/32+(n+3999)/4000+1;}
 void at(bool end){if(cut&&event==cut&&after==end)throw PowerCut{};}
 bool fails(bool end){return fail&&event==fail&&after==end;}
 InputStatus read(const char*k,void*out,uint32_t cap,uint32_t*n){++reads;++event;readKeys.push_back(k);at(false);*n=0;
  if(fails(false))return InputStatus::Unavailable;
  auto i=values.find(k);if(i==values.end()){at(true);return fails(true)?InputStatus::Unavailable:InputStatus::Missing;}
  if(i->second.size()>cap)return InputStatus::Invalid;
  memcpy(out,i->second.data(),i->second.size());*n=i->second.size();
  if(fails(true)){if(corruptRead)static_cast<uint8_t*>(out)[0]^=1;else if(wrongLength)--*n;else return InputStatus::Unavailable;}
  at(true);return InputStatus::Ready;
 }
 bool set(const char*k,const void*bytes,uint32_t n){++sets;++event;at(false);assert(!strcmp(k,"request")||!strcmp(k,"receipt")||!strcmp(k,"state"));
  if(fails(false)||(sets==failSet&&!after))return false;
  assert(!strcmp(k,"state")||!values.count(k)); // Only the one-byte scalar may update in place.
  size_t occupied=1+otherEntries;for(auto&v:values)occupied+=v.first=="receipt"?20:v.first=="state"?1:entries(v.second.size());
  const size_t added=!strcmp(k,"receipt")?20:!strcmp(k,"state")?1:entries(n);peak=std::max(peak,occupied+added);if(occupied+added>5*126)return false;
  values[k]={static_cast<const uint8_t*>(bytes),static_cast<const uint8_t*>(bytes)+n};at(true);return !fails(true)&&sets!=failSet;
 }
 bool commit(){++commits;++event;at(false);if(fails(false))return false;at(true);return !fails(true);}
 bool erase(const char*k){++erases;++event;at(false);assert(!strcmp(k,"request")||!strcmp(k,"receipt"));if(fails(false))return false;values.erase(k);at(true);return !fails(true);}
 InputStatus probe(const char*k){++probes;++event;at(false);if(fails(false))return InputStatus::Unavailable;auto result=values.count(k)?InputStatus::Ready:InputStatus::Missing;at(true);return fails(true)?InputStatus::Unavailable:result;}
 RequestTransport transport(){return {{this,[](void*c,const char*k,void*p,uint32_t n,uint32_t*out){return static_cast<Store*>(c)->read(k,p,n,out);}},
  [](void*c,const char*k,const void*p,uint32_t n){return static_cast<Store*>(c)->set(k,p,n);},[](void*c){return static_cast<Store*>(c)->commit();},
  [](void*c,const char*k){return static_cast<Store*>(c)->erase(k);},[](void*c,const char*k){return static_cast<Store*>(c)->probe(k);}};}
 void clearFaults(){reads=sets=commits=erases=probes=event=cut=fail=failSet=0;after=corruptRead=wrongLength=false;readKeys.clear();}
 std::vector<uint8_t> selected()const{return values.at(values.count("receipt")?"receipt":"request");}
};
struct Queue {
 Store store;RequestSource source;std::vector<uint8_t>old,next;uint8_t out[32]{};
 std::string current=profile(),time=R"({"schema":"riscrte.sntp","schema_version":1,"servers":["time.example.test"]})";
 Queue():old(RequestRecordBytes),next(RequestRecordBytes){source.storeAbi=3;source.firmwareSize=1234567;source.storeSha[0]=1;source.firmwareSha[0]=2;}
 int run(const std::string&p,const std::string&t){risc_provision_request_v1 q{sizeof(q),p.data(),uint32_t(p.size()),t.data(),uint32_t(t.size()),{}};
  memcpy(q.expected_active_store_sha256,source.storeSha,32);return queueRequest(store.transport(),q,source,old.data(),next.data(),next.size(),sha,out);}
 int run(){return run(current,time);}
 RequestView view(std::vector<uint8_t>&bytes){RequestView v;assert(parseRequest(bytes.data(),bytes.size(),v,sha));return v;}
 void first(){assert(run()==RISC_PROVISION_REQUEST_OK);store.clearFaults();}
 void consume(){auto&bytes=store.values.at("request");auto v=view(bytes);source.receiptAvailable=true;
  memcpy(source.consumedProfileSha,v.profileSha,32);memcpy(source.storeSha,v.targetStoreSha,32);assert(requestConsumed(v,source));store.clearFaults();}
 std::vector<uint8_t> receipt(){store.failSet=2;assert(run(profile(132,'b'),time)==RISC_PROVISION_REQUEST_UNKNOWN);
  auto result=store.values.at("receipt");assert(result.size()==RequestReceiptBytes);store.clearFaults();return result;}
};
void receiptIdentityAndCorruption(){
 Queue q;q.first();auto original=q.store.values.at("request");auto pending=q.view(original);uint8_t requestSha[32];memcpy(requestSha,pending.requestSha,32);
 q.consume();auto receipt=q.receipt();auto r=q.view(receipt);assert(r.terminal&&!r.profile&&!r.time);
 assert(r.profileSize==pending.profileSize&&r.timeSize==pending.timeSize);
 assert(!memcmp(r.source.storeSha,pending.source.storeSha,32)&&!memcmp(r.source.firmwareSha,pending.source.firmwareSha,32));
 assert(!memcmp(r.profileSha,pending.profileSha,32)&&!memcmp(r.requestSha,pending.requestSha,32)&&!memcmp(r.targetStoreSha,pending.targetStoreSha,32)&&!memcmp(r.timeSha,pending.timeSha,32));
 assert(!memcmp(r.consumedStoreSha,q.source.storeSha,32)&&requestConsumed(r,q.source)&&!requestSourceMatches(r,q.source));
 std::string raw(receipt.begin(),receipt.end());assert(raw.find("test-owner-network")==std::string::npos&&raw.find("test-only-password")==std::string::npos&&raw.find("riscrte.provisioning")==std::string::npos);
 Descriptor d;assert(loadDescriptor(requestInput(r),d)==InputStatus::Unavailable);
 assert(q.run()==RISC_PROVISION_REQUEST_OK&&!q.store.sets&&!memcmp(q.out,requestSha,32));
 // Receipt replay preserves identity; different time is not mistaken for the
 // old exact request merely because its profile hash is unchanged.
 auto time=q.time;time.insert(time.size()-1," ");assert(q.run(q.current,time)==RISC_PROVISION_REQUEST_OK&&q.store.sets==3);
 for(size_t i=0;i<receipt.size();++i){auto corrupt=receipt;corrupt[i]^=1;RequestView invalid;assert(!parseRequest(corrupt.data(),corrupt.size(),invalid,sha));}
 for(size_t n=0;n<receipt.size();++n){RequestView invalid;assert(!parseRequest(receipt.data(),n,invalid,sha));}
 auto extra=receipt;extra.push_back(0);RequestView invalid;assert(!parseRequest(extra.data(),extra.size(),invalid,sha));
 // All old v1 records remain independently readable, including consumed v1.
 assert(parseRequest(original.data(),original.size(),invalid,sha)&&!invalid.terminal&&invalid.profile);
}
void wrongBankAndPending(){
 Queue q;q.first();auto initial=q.store.values.at("request");assert(q.run(profile(132,'b'),q.time)==RISC_PROVISION_REQUEST_BUSY&&q.store.values.at("request")==initial&&!q.store.sets);
 q.consume();auto receipt=q.receipt();auto source=q.source;
 for(unsigned mode=0;mode<6;++mode){q.source=source;
  switch(mode){case 0:q.source.receiptAvailable=false;break;case 1:q.source.consumedProfileSha[31]^=1;break;
   case 2:q.source.storeSha[31]^=1;break;case 3:q.source.firmwareSha[31]^=1;break;case 4:++q.source.storeAbi;break;case 5:++q.source.firmwareSize;break;}
  auto r=q.view(receipt);assert(!requestConsumed(r,q.source)&&!requestSourceMatches(r,q.source));
  assert(q.run(profile(132,'b'),q.time)==RISC_PROVISION_REQUEST_BUSY&&q.store.values.at("receipt")==receipt&&!q.store.sets);
 }
 q.source=source;q.store.values["receipt"][0]^=1;assert(q.run()==RISC_PROVISION_REQUEST_INVALID&&!q.store.sets);
 q.store.values.erase("receipt");q.store.values["request"]=initial;q.source.storeSha[31]^=1;assert(q.run(profile(132,'b'),q.time)==RISC_PROVISION_REQUEST_BUSY&&!q.store.sets);
}
void durableCuts(){
 Queue baseline;baseline.first();baseline.consume();const auto initial=baseline.store.values.at("request");const auto source=baseline.source;
 const auto nextProfile=profile(132,'b');auto expectedReceipt=baseline.receipt();assert(baseline.run(nextProfile,baseline.time)==RISC_PROVISION_REQUEST_OK);const auto expectedNext=baseline.store.values.at("request");
 // Read control/state/old; write/verify terminal; ACTIVE; remove old;
 // fresh next; READY; remove terminal. All 23 operation boundaries.
 for(unsigned cut=1;cut<=23;++cut)for(bool after:{false,true}){
  Queue q;q.source=source;q.store.values["request"]=initial;q.store.values["state"]={RequestReady};q.store.cut=cut;q.store.after=after;bool interrupted=false;
  try{(void)q.run(nextProfile,q.time);}catch(const PowerCut&){interrupted=true;}assert(interrupted);
  const auto durable=q.store.selected();assert(durable==initial||durable==expectedReceipt||durable==expectedNext);
  q.store.clearFaults();assert(q.run(nextProfile,q.time)==RISC_PROVISION_REQUEST_OK&&q.store.values.at("request")==expectedNext&&!q.store.values.count("receipt"));
 }
 for(unsigned stage=1;stage<=23;++stage)for(bool after:{false,true}){
  Queue q;q.source=source;q.store.values["request"]=initial;q.store.values["state"]={RequestReady};q.store.after=after;q.store.fail=stage;
  const auto result=q.run(nextProfile,q.time);assert(result==(stage<=3?RISC_PROVISION_REQUEST_UNAVAILABLE:RISC_PROVISION_REQUEST_UNKNOWN));assert(q.store.event==stage);
  const auto durable=q.store.selected();assert(durable==initial||durable==expectedReceipt||durable==expectedNext);for(auto byte:q.out)assert(!byte);
  q.store.clearFaults();assert(q.run(nextProfile,q.time)==RISC_PROVISION_REQUEST_OK&&!q.store.values.count("receipt"));
 }
 for(unsigned stage:{6u,10u,16u,20u})for(bool corrupt:{false,true}){
  Queue q;q.source=source;q.store.values["request"]=initial;q.store.values["state"]={RequestReady};q.store.after=true;q.store.fail=stage;q.store.corruptRead=corrupt;q.store.wrongLength=!corrupt;
  assert(q.run(nextProfile,q.time)==RISC_PROVISION_REQUEST_UNKNOWN&&q.store.event==stage);
  q.store.clearFaults();assert(q.run(nextProfile,q.time)==RISC_PROVISION_REQUEST_OK&&!q.store.values.count("receipt"));
 }
 // Control always wins over a staged payload, even when its bytes are invalid.
 Queue q;q.source=source;q.store.values["state"]={RequestActive};q.store.values["receipt"]=expectedReceipt;q.store.values["request"]={1,2,3};
 assert(q.run()==RISC_PROVISION_REQUEST_OK&&q.store.readKeys==std::vector<std::string>{"receipt"}&&!q.store.sets&&!q.store.erases);
 q.store.clearFaults();assert(q.run(nextProfile,q.time)==RISC_PROVISION_REQUEST_OK&&!q.store.values.count("receipt"));
}
void finiteCapacityAndBounds(){
 Queue q;q.first();q.consume();const auto old=q.store.values.at("request");
 assert(Store::entries(old.size())*2+1+q.store.otherEntries>5*126);
 // Repeated changed cohorts work because only a terminal receipt overlaps the
 // new full request. The model never frees the old generation early.
 for(unsigned i=0;i<10;++i){const auto next=profile(i%2?125:132,i%2?'a':'b');
  assert(q.run(next,q.time)==RISC_PROVISION_REQUEST_OK&&q.store.sets==4);
  auto bytes=q.store.values.at("request");assert(!q.view(bytes).terminal);q.consume();}
 auto maximum=profile(3);maximum.resize(ProfileInputBytes,' ');auto time=q.time;time.resize(384,' ');
 assert(q.run(maximum,time)==RISC_PROVISION_REQUEST_OK);q.consume();assert(q.store.values.at("request").size()==RequestRecordBytes);
 maximum[maximum.find("test-owner")]='b';assert(q.run(maximum,time)==RISC_PROVISION_REQUEST_OK);q.consume();
 // Finite unrelated occupancy may still refuse a new request safely. Its
 // proven-consumed receipt remains durable and retryable; no unrelated erase.
 assert(q.run(profile(125,'c'),q.time)==RISC_PROVISION_REQUEST_OK);q.consume();
 q.store.otherEntries=200;assert(q.run(maximum+" ",time)==RISC_PROVISION_REQUEST_INVALID&&!q.store.sets);
 auto next=profile(160,'d');assert(q.run(next,q.time)==RISC_PROVISION_REQUEST_UNKNOWN);
 auto receipt=q.store.values.at("receipt");assert(q.view(receipt).terminal);
 q.store.clearFaults();q.store.otherEntries=32;assert(q.run(next,q.time)==RISC_PROVISION_REQUEST_OK);
 assert(q.store.peak>5*126); // Refusal is exercised, not silently unlimited.
}
void actualFeed(const char*path){
 std::ifstream stream(path,std::ios::binary);assert(stream);std::string text{std::istreambuf_iterator<char>(stream),{}};
 JsonDocument feed;assert(!deserializeJson(feed,text));JsonObject p=feed["boot_profiles"]["core"]["profile"];
 assert(p["files"].size()==125||p["files"].size()==132);
 for(bool maximumEscaping:{false,true}){
  p["wifi"]["ssid"]=maximumEscaping?std::string(32,'\\'):std::string("test-owner-network");
  p["wifi"]["password"]=maximumEscaping?std::string(63,'"'):std::string("test-only-password");
  Queue q;q.current=encode(p);q.time=encode(feed["boot_profiles"]["core"]["time"]);q.first();q.consume();
  const size_t n=q.store.values.at("request").size();assert(2*n>0x6000);
  p["image"]["sha256"]=std::string(64,maximumEscaping?'f':'e');assert(q.run(encode(p),q.time)==RISC_PROVISION_REQUEST_OK&&q.store.sets==4);
  printf("Actual %u-file public feed: %s test-only private record %zu bytes, consumed compaction replacement PASS\n",unsigned(p["files"].size()),maximumEscaping?"maximum-escaped":"ordinary",n);
 }
}
}
int main(int argc,char**argv){static_assert(RequestReceiptBytes==288&&RequestRecordBytes==16928,"fixed receipt/native input bounds");
 receiptIdentityAndCorruption();wrongBankAndPending();durableCuts();finiteCapacityAndBounds();for(int i=1;i<argc;++i)actualFeed(argv[i]);
 puts("Request compaction: credential-free terminal v2, v1 compatibility, exact replay, wrong-bank fences, all durable-operation cuts, finite entries/max bounds/refusal and retry PASS");
}
