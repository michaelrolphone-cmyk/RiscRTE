#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "nvs.h"
#include "nvs_flash.h"
#include "nvs_partition_manager.hpp"
#include "test_fixtures.hpp"
#include "runtime/provisioning/RequestQueue.h"
#include "ports/esp32s3/NvsProvisionRequestStorage.h"
#include "bootstrap/Json.h"
// Compile the production global latch and native KV/settings/bootstrap adapters.
// Direct reset below models a new process; no production reset API exists.
#define ESP_PLATFORM 1
#include "ports/esp32s3/NvsKeyValue.cpp"
#define RISC_PAIRED_BANKS 1
#include "ports/esp32s3/NvsBootstrapInput.h"
#include "ports/esp32s3/OwnerNvsInstaller.cpp"
#undef ESP_PLATFORM
namespace RiscCpu {bool nativeRadioResourceTry(){return true;}void nativeRadioResourceEnd(){}}
static unsigned nativeOpenCalls=0;
extern "C" esp_err_t __real_nvs_flash_init(){return ESP_OK;}
extern "C" esp_err_t __real_nvs_open(const char*,nvs_open_mode_t,nvs_handle_t*);
extern "C" esp_err_t __wrap_nvs_open(const char*ns,nvs_open_mode_t mode,nvs_handle_t*h){++nativeOpenCalls;return __real_nvs_open(ns,mode,h);}
static void ordinaryBootLatch(){RiscNvs::detail::mutationQuarantined.store(false);::attempted=false;::status.store(ESP_ERR_INVALID_STATE);REQUIRE(__wrap_nvs_flash_init()==ESP_OK);}
#include <openssl/sha.h>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <memory>
#include <vector>
using Blob=std::vector<uint8_t>;
using namespace RiscProvision;
static bool sha(const void*p,uint32_t n,uint8_t*out){return SHA256(static_cast<const uint8_t*>(p),n,out)!=nullptr;}
static Blob file(const std::string&p){std::ifstream f(p,std::ios::binary);REQUIRE(f.good());return Blob(std::istreambuf_iterator<char>(f),{});}
struct Cut {};
struct SnapshotFlash: SpiFlashEmulator {
 SnapshotFlash():SpiFlashEmulator(6){}
 void restore(const std::vector<uint32_t>&v){REQUIRE(v.size()==mData.size());mData=v;clearStats();mFailCountdown=SIZE_MAX;}
 void clearFailure(){mFailCountdown=SIZE_MAX;}
 std::vector<uint32_t> snapshot(){return mData;}
};
struct CutPartition: PartitionEmulation {
 bool abrupt=false;
 CutPartition(SnapshotFlash*e):PartitionEmulation(e,0,6*4096,"nvs"){}
 esp_err_t write(size_t o,const void*p,size_t n)override{auto e=PartitionEmulation::write(o,p,n);if(e!=ESP_OK&&abrupt)throw Cut{};return e;}
 esp_err_t write_raw(size_t o,const void*p,size_t n)override{auto e=PartitionEmulation::write_raw(o,p,n);if(e!=ESP_OK&&abrupt)throw Cut{};return e;}
 esp_err_t erase_range(size_t o,size_t n)override{auto e=PartitionEmulation::erase_range(o,n);if(e!=ESP_OK&&abrupt)throw Cut{};return e;}
};
struct RealNVS {
 SnapshotFlash flash;CutPartition part{&flash};nvs_handle_t request=0,other=0;
 std::vector<std::pair<std::string,Blob>> writes;bool selecting=false,unknown=false;unsigned payloadReads=0,inputReads=0;
 RealNVS(const std::vector<uint32_t>*state=nullptr){ordinaryBootLatch();if(state)flash.restore(*state);open();}
 void open(){REQUIRE(nvs::NVSPartitionManager::get_instance()->init_custom(&part,0,6)==ESP_OK);REQUIRE(nvs_open("rte_provreq",NVS_READWRITE,&request)==ESP_OK);REQUIRE(nvs_open("rte00000001",NVS_READWRITE,&other)==ESP_OK);}
 void close(){nvs_close(request);nvs_close(other);REQUIRE(nvs_flash_deinit_partition("nvs")==ESP_OK);}
 ~RealNVS(){close();}
 void reboot(){part.abrupt=false;flash.clearFailure();close();unknown=false;ordinaryBootLatch();open();}
 esp_err_t put(const Blob&b){auto e=nvs_set_blob(request,"request",b.data(),b.size());return e==ESP_OK?nvs_commit(request):e;}
 bool get(Blob&b){size_t n=0;if(nvs_get_blob(request,"request",nullptr,&n)!=ESP_OK)return false;b.resize(n);return nvs_get_blob(request,"request",b.data(),&n)==ESP_OK;}
 Blob get(){Blob b;REQUIRE(get(b));return b;}
 static Blob unrelatedBlob(){Blob b(768);for(size_t i=0;i<b.size();++i)b[i]=uint8_t(i*73+19);return b;}
 void sentinel(){REQUIRE(nvs_set_str(other,"sentinel","preserve-this-key")==ESP_OK);auto b=unrelatedBlob();REQUIRE(nvs_set_blob(other,"variable",b.data(),b.size())==ESP_OK);REQUIRE(nvs_commit(other)==ESP_OK);}
 void verifyOther(unsigned count=0){char v[32];size_t n=sizeof(v);REQUIRE(nvs_get_str(other,"sentinel",v,&n)==ESP_OK);REQUIRE(std::string(v)=="preserve-this-key");auto expected=unrelatedBlob();Blob actual(expected.size());n=actual.size();REQUIRE(nvs_get_blob(other,"variable",actual.data(),&n)==ESP_OK);REQUIRE(n==expected.size());REQUIRE(actual==expected);uint32_t nativeSize=0;std::fill(actual.begin(),actual.end(),0);REQUIRE(RiscNvs::get(nullptr,1,"variable",actual.data(),actual.size(),&nativeSize)==RISC_KEY_VALUE_OK);REQUIRE(nativeSize==expected.size());REQUIRE(actual==expected);for(unsigned i=0;i<count;i++){char k[16];snprintf(k,sizeof(k),"key%u",i);uint32_t value=0;REQUIRE(nvs_get_u32(other,k,&value)==ESP_OK);REQUIRE(value==0xCAFE0000+i);}}
 unsigned fill(unsigned count){unsigned i=0;for(;i<count;i++){char k[16];snprintf(k,sizeof(k),"key%u",i);if(nvs_set_u32(other,k,0xCAFE0000+i)!=ESP_OK)break;}REQUIRE(nvs_commit(other)==ESP_OK);return i;}
 RequestTransport transport(){return {{this,[](void*c,const char*k,void*out,uint32_t cap,uint32_t*n){auto&s=*static_cast<RealNVS*>(c);REQUIRE_FALSE(s.unknown);++s.inputReads;if(!strcmp(k,"request")){++s.payloadReads;if(s.selecting)REQUIRE(RiscProvisionRequestStorage::probe(s.request,"receipt")==InputStatus::Missing);}return RiscProvisionRequestStorage::read(s.request,k,out,cap,n);}},
 [](void*c,const char*k,const void*p,uint32_t n){auto&s=*static_cast<RealNVS*>(c);s.writes.emplace_back(k,Blob(static_cast<const uint8_t*>(p),static_cast<const uint8_t*>(p)+n));return RiscProvisionRequestStorage::set(s.request,k,p,n);},
 [](void*c){return nvs_commit(static_cast<RealNVS*>(c)->request)==ESP_OK;},
 [](void*c,const char*k){return RiscProvisionRequestStorage::erase(static_cast<RealNVS*>(c)->request,k);},
 [](void*c,const char*k){return RiscProvisionRequestStorage::probe(static_cast<RealNVS*>(c)->request,k);}};}
 InputStatus status(Blob&out){if(unknown)return InputStatus::Unavailable;out.resize(RequestRecordBytes);uint32_t n=0;selecting=true;auto result=readRequest(transport().input,out.data(),out.size(),&n);selecting=false;if(result!=InputStatus::Ready)return result;out.resize(n);RequestView v;return parseRequest(out.data(),out.size(),v,sha)?InputStatus::Ready:InputStatus::Invalid;}
 bool selected(Blob&out){return status(out)==InputStatus::Ready;}
 void stats(const std::string&label){nvs_stats_t s{};REQUIRE(nvs_get_stats("nvs",&s)==ESP_OK);std::cout<<label<<" used="<<s.used_entries<<" free="<<s.free_entries<<" total="<<s.total_entries<<" writes="<<flash.getWriteOps()<<" erases="<<flash.getEraseOps()<<"\n";}
};
static std::string json(JsonVariantConst v){std::string s;serializeJson(v,s);return s;}
struct QueueData {
 Blob profile,time;RequestSource source;Blob oldBuffer=Blob(RequestRecordBytes),nextBuffer=Blob(RequestRecordBytes);uint8_t id[32];bool maximum=false;unsigned changeCount=0;
 QueueData(const std::string&device,bool max=false,bool escaped=false):maximum(max){
  const auto bytes=file(std::string(RISC_TEST_FIXTURES)+"/request-profile-"+device+(device=="watch"?"-125.json":"-132.json"));JsonDocument feed;REQUIRE(!deserializeJson(feed,bytes.data(),bytes.size()));
  auto p=feed["boot_profiles"]["core"]["profile"].as<JsonObject>();p["wifi"]["ssid"]=escaped?std::string(32,'\\'):std::string("test-owner-network");p["wifi"]["password"]=escaped?std::string(63,'"'):std::string("test-only-password");
  auto text=json(p);profile={text.begin(),text.end()};text=json(feed["boot_profiles"]["core"]["time"]);time={text.begin(),text.end()};
  source.storeAbi=3;source.firmwareSize=1234567;source.storeSha[0]=1;source.firmwareSha[0]=2;if(maximum){profile.resize(ProfileInputBytes,' ');time.resize(384,' ');}
 }
 int queue(RealNVS&n){if(n.unknown)return RISC_PROVISION_REQUEST_UNKNOWN;risc_provision_request_v1 q{sizeof(q),profile.data(),uint32_t(profile.size()),time.data(),uint32_t(time.size()),{}};memcpy(q.expected_active_store_sha256,source.storeSha,32);auto rc=queueRequest(n.transport(),q,source,oldBuffer.data(),nextBuffer.data(),RequestRecordBytes,sha,id);if(rc==RISC_PROVISION_REQUEST_UNKNOWN){n.unknown=true;RiscNvs::quarantine();}return rc;}
 void consume(const Blob&b){auto copy=b;RequestView v;REQUIRE(parseRequest(copy.data(),copy.size(),v,sha));REQUIRE_FALSE(v.terminal);source.receiptAvailable=true;memcpy(source.consumedProfileSha,v.profileSha,32);memcpy(source.storeSha,v.targetStoreSha,32);}
 void change(){JsonDocument p;REQUIRE(!deserializeJson(p,profile.data(),profile.size()));p["image"]["sha256"]=std::string(64,++changeCount%2?'e':'f');auto text=json(p);profile={text.begin(),text.end()};if(maximum)profile.resize(ProfileInputBytes,' ');}
 void makeMaximum(){maximum=true;profile.resize(ProfileInputBytes,' ');time.resize(384,' ');}
};
static Blob encode(const Blob&old,QueueData&q){Blob b(RequestHeaderBytes+q.profile.size()+q.time.size());std::copy(old.begin(),old.begin()+160,b.begin());auto put=[&](size_t at,uint32_t v){for(unsigned i=0;i<4;i++)b[at+i]=v>>(i*8);};put(12,b.size());put(16,q.profile.size());put(20,q.time.size());memcpy(b.data()+32,q.source.storeSha,32);std::copy(q.profile.begin(),q.profile.end(),b.begin()+160);std::copy(q.time.begin(),q.time.end(),b.begin()+160+q.profile.size());sha(q.profile.data(),q.profile.size(),b.data()+96);memset(b.data()+128,0,32);uint8_t sum[32];sha(b.data(),b.size(),sum);memcpy(b.data()+128,sum,32);return b;}
static void priorGeneration(RealNVS&n,const Blob&old,int generation){if(generation){REQUIRE(n.put(Blob(288,0xa5))==ESP_OK);REQUIRE(n.put(Blob(288,0x5a))==ESP_OK);REQUIRE(n.put(old)==ESP_OK);}}
static void published(RealNVS&n,const Blob&next){uint8_t marker=0;REQUIRE(nvs_get_u8(n.request,"state",&marker)==ESP_OK);REQUIRE(marker==RequestReady);REQUIRE(RiscProvisionRequestStorage::probe(n.request,"receipt")==InputStatus::Missing);Blob actual;REQUIRE(n.selected(actual));REQUIRE(actual==next);}
// This latch models the native bridge's process-local UNKNOWN guard. Its exact
// status/queue/boot no-open behavior is covered by native_provision_request_test.
static void fenced(RealNVS&n,QueueData&q){
 const auto reads=n.inputReads,opens=nativeOpenCalls;auto writes=n.writes.size();auto flash=n.flash.snapshot();Blob ignored;
 REQUIRE(RiscNvs::initializationStatus()==ESP_FAIL);REQUIRE(__wrap_nvs_flash_init()==ESP_FAIL);
 REQUIRE(n.status(ignored)==InputStatus::Unavailable);REQUIRE(n.status(ignored)==InputStatus::Unavailable);
 REQUIRE(q.queue(n)==RISC_PROVISION_REQUEST_UNKNOWN);REQUIRE(n.inputReads==reads);REQUIRE(n.writes.size()==writes);
 const auto*kv=RiscNvs::backend();const auto*settings=kv->scalableSettings;
 uint8_t bytes[32]{};uint32_t size=0;bool exists=false;char key[16]{};
 REQUIRE(kv->get(nullptr,1,"variable",bytes,sizeof bytes,&size)==RISC_KEY_VALUE_IO);
 REQUIRE(kv->get(nullptr,1,"variable",nullptr,0,&size)==RISC_KEY_VALUE_IO);
 REQUIRE(kv->put(nullptr,1,"variable",bytes,sizeof bytes)==RISC_KEY_VALUE_IO);
 REQUIRE(kv->journalRead(nullptr,1,bytes,sizeof bytes,&size)==RISC_KEY_VALUE_IO);
 REQUIRE(kv->journalWrite(nullptr,1,bytes,sizeof bytes)==RISC_KEY_VALUE_IO);
 REQUIRE(kv->legacyNamespace(nullptr,1,&exists)==RISC_KEY_VALUE_IO);
 REQUIRE(settings->anchorRead(nullptr,1,bytes,sizeof bytes,&size)==RISC_KEY_VALUE_IO);
 REQUIRE(settings->anchorWrite(nullptr,1,bytes,sizeof bytes)==RISC_KEY_VALUE_IO);
 REQUIRE(settings->next(nullptr,1,false,"",key,bytes,sizeof bytes,&size)==RISC_KEY_VALUE_IO);
 REQUIRE(settings->deltaWrite(nullptr,1,"delta",bytes,sizeof bytes)==RISC_KEY_VALUE_IO);
 REQUIRE(settings->deltaErase(nullptr,1,"delta")==RISC_KEY_VALUE_IO);
 REQUIRE(settings->legacySnapshot(nullptr,1,bytes,sizeof bytes,&size)==RISC_KEY_VALUE_IO);
 auto bootstrap=RiscBootstrap::nvsInput();
 REQUIRE(bootstrap.read(bootstrap.context,"profile",bytes,sizeof bytes,&size)==InputStatus::Unavailable);
 Blob scratch(ProfileInputBytes);
 REQUIRE(RiscBootstrap::installOwnerNvs(q.profile.data(),q.profile.size(),q.time.data(),q.time.size(),scratch.data(),scratch.size(),[](){return true;})==InstallResult::Unavailable);
 REQUIRE(nativeOpenCalls==opens);REQUIRE(n.flash.snapshot()==flash);
}
static bool known(RealNVS&n,const Blob&old,const Blob&receipt,const Blob&next){Blob b;if(!n.selected(b))return false;return b==old||b==receipt||b==next;}
TEST_CASE("actual125and132 repeated changes and maximum escaping", "[production][repeat]"){
 for(const std::string device:{"watch","wave"})for(bool maximum:{false,true})for(bool escaped:{false,true}){
  RealNVS n;QueueData q(device,maximum,escaped);n.sentinel();REQUIRE(q.queue(n)==RISC_PROVISION_REQUEST_OK);auto initial=n.get();std::cout<<"PRODUCTION "<<device<<" maximum="<<maximum<<" escaped="<<escaped<<" initial-bytes="<<initial.size()<<std::endl;
  for(unsigned turn=0;turn<12;turn++){auto old=n.get();q.consume(old);q.change();auto next=encode(old,q);REQUIRE(q.queue(n)==RISC_PROVISION_REQUEST_OK);published(n,next);n.reboot();published(n,next);n.verifyOther();auto writes=n.writes.size();REQUIRE(q.queue(n)==RISC_PROVISION_REQUEST_OK);REQUIRE(n.writes.size()==writes);}
 }
}
TEST_CASE("pressure refusal is terminal and unrelated data survives", "[production][capacity]"){
 for(const std::string device:{"watch","wave"})for(int generation:{0,1})for(unsigned filler:{0u,80u,90u,100u,150u,200u}){
  RealNVS n;QueueData q(device);n.sentinel();REQUIRE(q.queue(n)==RISC_PROVISION_REQUEST_OK);auto old=n.get();priorGeneration(n,old,generation);unsigned filled=n.fill(filler);q.consume(old);q.change();q.makeMaximum();auto next=encode(old,q);n.writes.clear();auto result=q.queue(n);Blob receipt;if(!n.writes.empty()&&n.writes.front().first=="receipt")receipt=n.writes.front().second;
  if(result==RISC_PROVISION_REQUEST_OK)published(n,next);else{REQUIRE(result==RISC_PROVISION_REQUEST_UNKNOWN);fenced(n,q);}
  if(!n.unknown)n.verifyOther(filled);n.reboot();REQUIRE(known(n,old,receipt,next));n.verifyOther(filled);if(result==RISC_PROVISION_REQUEST_OK)published(n,next);
  std::cout<<"PRODUCTION CAPACITY "<<device<<" generation="<<generation<<" filler="<<filled<<" result="<<result<<std::endl;
 }
}
TEST_CASE("every flash boundary preserves selection and reconciliation", "[production][cuts]"){
 for(const std::string device:{"watch","wave"})for(int generation:{0,1})for(unsigned filler:{0u,90u}){
  QueueData q(device);Blob old,receipt,next;std::vector<uint32_t>state;size_t steps;bool fits;
  {RealNVS n;n.sentinel();REQUIRE(q.queue(n)==RISC_PROVISION_REQUEST_OK);old=n.get();priorGeneration(n,old,generation);REQUIRE(n.fill(filler)==filler);q.consume(old);q.change();q.makeMaximum();next=encode(old,q);state=n.flash.snapshot();n.flash.clearStats();n.writes.clear();fits=q.queue(n)==RISC_PROVISION_REQUEST_OK;REQUIRE(!n.writes.empty());REQUIRE(n.writes.front().first=="receipt");receipt=n.writes.front().second;steps=n.flash.getWriteBytes()/4+n.flash.getEraseOps();}
  unsigned unavailable=0,success=0;
  for(bool abrupt:{true,false})for(size_t cut=0;cut<=steps;cut++){
   INFO("device="<<device<<" generation="<<generation<<" filler="<<filler<<" cut="<<cut<<"/"<<steps<<" abrupt="<<abrupt);
   RealNVS n(&state);n.flash.clearStats();n.part.abrupt=abrupt;n.flash.failAfter(cut);int result=RISC_PROVISION_REQUEST_UNKNOWN;try{result=q.queue(n);}catch(const Cut&){}
   n.flash.clearFailure();
   // Abrupt power loss must reboot without a status read or cleanup in the
   // interrupted process. Returned I/O errors remain UNKNOWN for this boot;
   // no reconciliation may touch the potentially damaged live queue cache.
   if(!abrupt&&result!=RISC_PROVISION_REQUEST_OK){fenced(n,q);++unavailable;}
   if(!abrupt&&!n.unknown)n.verifyOther(filler);
   if(result==RISC_PROVISION_REQUEST_OK){published(n,next);++success;}
   n.reboot();REQUIRE(known(n,old,receipt,next));n.verifyOther(filler);if(result==RISC_PROVISION_REQUEST_OK)published(n,next);
   Blob current;REQUIRE(n.selected(current));if(current!=next){auto retry=q.queue(n);if(fits)REQUIRE(retry==RISC_PROVISION_REQUEST_OK);if(retry==RISC_PROVISION_REQUEST_OK)published(n,next);else{fenced(n,q);n.reboot();REQUIRE(known(n,old,receipt,next));}}n.verifyOther(filler);
  }
  std::cout<<"PRODUCTION CUT PASS "<<device<<" generation="<<generation<<" filler="<<filler<<" fits="<<fits<<" boundaries="<<2*(steps+1)<<" success="<<success<<" immediate-unavailable="<<unavailable<<std::endl;
 }
}

TEST_CASE("receipt CRC self-erasure stays fenced across reads and reboot", "[production][crc]"){
 RealNVS n;QueueData q("watch");n.sentinel();REQUIRE(q.queue(n)==RISC_PROVISION_REQUEST_OK);auto old=n.get();REQUIRE(n.fill(90)==90);q.consume(old);q.change();q.makeMaximum();n.writes.clear();REQUIRE(q.queue(n)==RISC_PROVISION_REQUEST_UNKNOWN);fenced(n,q);n.reboot();
 uint8_t marker=0;REQUIRE(nvs_get_u8(n.request,"state",&marker)==ESP_OK);REQUIRE(marker==RequestActive);REQUIRE(n.writes.front().first=="receipt");
 static const char hex[]="0123456789abcdef";std::string needle;for(auto v:n.writes.front().second){needle+=hex[v>>4];needle+=hex[v&15];}
 auto flash=n.flash.snapshot();auto*begin=reinterpret_cast<uint8_t*>(flash.data());auto*end=begin+flash.size()*sizeof(uint32_t);auto at=std::search(begin,end,needle.begin(),needle.end());REQUIRE(at!=end);at[80]^=1;
 n.close();n.flash.restore(flash);n.open();const auto reads=n.payloadReads;Blob current;
 REQUIRE(n.status(current)==InputStatus::Invalid);REQUIRE(RiscProvisionRequestStorage::probe(n.request,"receipt")==InputStatus::Missing);
 for(unsigned i=0;i<4;i++)REQUIRE(n.status(current)==InputStatus::Invalid);REQUIRE(n.payloadReads==reads);n.reboot();REQUIRE(n.status(current)==InputStatus::Invalid);REQUIRE(n.payloadReads==reads);n.verifyOther(90);
 std::cout<<"PRODUCTION CRC: self-erased terminal remains ACTIVE/BLOCKED without payload or legacy reads across repeated reads/reboot\n";
}
TEST_CASE("READY never restores legacy bootstrap after record loss", "[production][crc]"){
 RealNVS n;QueueData q("wave");n.sentinel();REQUIRE(q.queue(n)==RISC_PROVISION_REQUEST_OK);REQUIRE(nvs_erase_key(n.request,"request")==ESP_OK);REQUIRE(nvs_commit(n.request)==ESP_OK);Blob current;REQUIRE(n.status(current)==InputStatus::Invalid);n.reboot();REQUIRE(n.status(current)==InputStatus::Invalid);n.verifyOther();
}
TEST_CASE("READY initialization on a legacy pending replay survives every cut", "[production][scalar-init]"){
 QueueData q("watch");Blob pending;std::vector<uint32_t>state;size_t steps;
 {RealNVS n;n.sentinel();REQUIRE(q.queue(n)==RISC_PROVISION_REQUEST_OK);pending=n.get();REQUIRE(nvs_erase_key(n.request,"state")==ESP_OK);REQUIRE(nvs_commit(n.request)==ESP_OK);state=n.flash.snapshot();n.flash.clearStats();REQUIRE(q.queue(n)==RISC_PROVISION_REQUEST_OK);published(n,pending);steps=n.flash.getWriteBytes()/4+n.flash.getEraseOps();}
 for(bool abrupt:{false,true})for(size_t cut=0;cut<=steps;cut++){
  INFO("initial READY cut="<<cut<<" abrupt="<<abrupt);RealNVS n(&state);n.part.abrupt=abrupt;n.flash.failAfter(cut);int result=RISC_PROVISION_REQUEST_UNKNOWN;try{result=q.queue(n);}catch(const Cut&){}
  n.flash.clearFailure();if(!abrupt&&result!=RISC_PROVISION_REQUEST_OK)fenced(n,q);if(!abrupt&&!n.unknown)n.verifyOther();if(result==RISC_PROVISION_REQUEST_OK)published(n,pending);
  n.reboot();Blob actual;REQUIRE(n.selected(actual));REQUIRE(actual==pending);REQUIRE(q.queue(n)==RISC_PROVISION_REQUEST_OK);published(n,pending);n.verifyOther();
 }
 std::cout<<"PRODUCTION initial READY boundaries="<<2*(steps+1)<<" PASS\n";
}
TEST_CASE("first enqueue from empty queue survives every flash boundary", "[production][first]"){
 for(const std::string device:{"watch","wave"})for(bool maximum:{false,true}){
  QueueData q(device,maximum);Blob pending;std::vector<uint32_t>state;size_t steps;
  {RealNVS n;n.sentinel();state=n.flash.snapshot();n.flash.clearStats();REQUIRE(q.queue(n)==RISC_PROVISION_REQUEST_OK);pending=n.get();published(n,pending);steps=n.flash.getWriteBytes()/4+n.flash.getEraseOps();}
  for(bool abrupt:{false,true})for(size_t cut=0;cut<=steps;cut++){
   INFO("first device="<<device<<" maximum="<<maximum<<" cut="<<cut<<" abrupt="<<abrupt);RealNVS n(&state);n.part.abrupt=abrupt;n.flash.failAfter(cut);int result=RISC_PROVISION_REQUEST_UNKNOWN;try{result=q.queue(n);}catch(const Cut&){}
   n.flash.clearFailure();if(!abrupt&&result!=RISC_PROVISION_REQUEST_OK)fenced(n,q);if(!abrupt&&!n.unknown)n.verifyOther();if(result==RISC_PROVISION_REQUEST_OK)published(n,pending);
   n.reboot();Blob actual;auto status=n.status(actual);REQUIRE((status==InputStatus::Missing||status==InputStatus::Ready));if(status==InputStatus::Ready)REQUIRE(actual==pending);
   if(result==RISC_PROVISION_REQUEST_OK)published(n,pending);REQUIRE(q.queue(n)==RISC_PROVISION_REQUEST_OK);published(n,pending);n.verifyOther();
  }
  std::cout<<"PRODUCTION FIRST "<<device<<" maximum="<<maximum<<" boundaries="<<2*(steps+1)<<" PASS"<<std::endl;
 }
}
