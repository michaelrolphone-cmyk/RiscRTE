#include "native_replica_checked_fixture.h"
#include "experimental/native_replica/CheckedNvsLegacySources.h"
#include "runtime/storage/SdFirstKeyValue.h"
#include <openssl/evp.h>
#include <algorithm>
#include <set>
namespace F=NativeFixture;
using A=RiscExperimental::NativeBaselineAdmission;
using Sources=RiscExperimental::CheckedNvsLegacySources;
using Old=RiscStorage::SdFirstKeyValue;
using Rows=std::map<std::string,Bytes>;
static const uint8_t Owner[16]={'R','i','s','c','R','T','E','/','N','a','t','i','v','e','1',0};
static const char LegacyOwner[]="shared-00000006";
static uint64_t epoch=19;
static bool retained=false,retainWhileOpen=false,loseWhileOpen=false,lateGuard=false,lateInit=false,epochWhileOpen=false,reenter=false;
static bool reverseOrder=false,hideFind=false,duplicateInfo=false;
static unsigned corruptInfo=0,reentries=0;
static Sources*active=nullptr;
struct Iterator {std::string space;std::vector<std::string> keys;size_t at=0;};
static std::set<Iterator*> iterators;
extern "C" esp_err_t nvs_get_used_entry_count(nvs_handle_t h,size_t*n){
 ++F::nvsTotal;F::safe();if(F::hit("nvs-count"))return ESP_FAIL;assert(F::handles.count(h));*n=0;for(auto&r:F::nvs.at(F::handles.at(h).name))*n+=2+(r.second.size()+31)/32;return ESP_OK;
}
static nvs_iterator_t fixture_entry_find(const char*partition,const char*space,nvs_type_t type){
 ++F::nvsTotal;F::safe();assert(!strcmp(partition,"nvs")&&type==NVS_TYPE_ANY);
 if(hideFind||F::hit("nvs-find"))return nullptr;
 auto found=F::nvs.find(space);if(found==F::nvs.end()||found->second.empty())return nullptr;
 auto*i=new Iterator;iterators.insert(i);i->space=space;for(auto&e:found->second)i->keys.push_back(e.first);
 if(reverseOrder)std::reverse(i->keys.begin(),i->keys.end());
 if(duplicateInfo)i->keys.push_back(i->keys.front());
 return i;
}
static nvs_iterator_t fixture_entry_next(nvs_iterator_t it){
 ++F::nvsTotal;F::safe();auto*i=static_cast<Iterator*>(it);assert(iterators.count(i));if(++i->at<i->keys.size())return i;iterators.erase(i);delete i;return nullptr;
}
static void fixture_entry_info(nvs_iterator_t it,nvs_entry_info_t*out){
 ++F::nvsTotal;F::safe();auto*i=static_cast<Iterator*>(it);assert(iterators.count(i));*out={};strcpy(out->namespace_name,i->space.c_str());strcpy(out->key,i->keys.at(i->at).c_str());out->type=NVS_TYPE_BLOB;
 if(corruptInfo==1)out->type=NVS_TYPE_U8;
 if(corruptInfo==2)memset(out->key,'a',16);
 if(corruptInfo==3)strcpy(out->key,"Bad");
 if(corruptInfo==4)out->namespace_name[3]='9';
 if(corruptInfo==5)memset(out->namespace_name,'a',16);
}
#include "native_nvs_iterator_fixture.inc"
extern "C" void nvs_release_iterator(nvs_iterator_t it){++F::nvsTotal;F::safe();auto*i=static_cast<Iterator*>(it);if(!i)return;assert(iterators.erase(i));delete i;}
struct Hash {
 EVP_MD_CTX*ctx=EVP_MD_CTX_new();unsigned calls=0,failAt=0;bool changeAfterFinish=false,retainAfterFinish=false;
 ~Hash(){EVP_MD_CTX_free(ctx);}
 A::Digest api(){return {this,
  [](void*p){auto&h=*static_cast<Hash*>(p);return ++h.calls!=h.failAt&&EVP_DigestInit_ex(h.ctx,EVP_sha256(),nullptr)==1;},
  [](void*p,const void*b,uint32_t n){assert(n<=4096);auto&h=*static_cast<Hash*>(p);return ++h.calls!=h.failAt&&EVP_DigestUpdate(h.ctx,b,n)==1;},
  [](void*p,uint8_t*out){auto&h=*static_cast<Hash*>(p);unsigned n=0;bool ok=++h.calls!=h.failAt&&EVP_DigestFinal_ex(h.ctx,out,&n)==1&&n==32;if(h.changeAfterFinish){F::nvs["rte00000006"]["original"]={9};h.changeAfterFinish=false;}if(h.retainAfterFinish){retained=true;F::lease=false;}return ok;}};}
};
static Sources::Hooks hooks(){return {nullptr,
 [](void*){if(lateGuard){retained=true;F::lease=false;}return F::onWorker&&F::lease;},
 [](void*){return epoch;},
 [](void*){if(lateInit)F::lease=false;return F::initialized?ESP_OK:ESP_ERR_NVS_NOT_INITIALIZED;},
 [](void*){if(epochWhileOpen&&!F::handles.empty())++epoch;if((retainWhileOpen||loseWhileOpen)&&!F::handles.empty()){retained=retainWhileOpen;F::lease=false;}if(reenter&&active){reenter=false;A::Witness w;assert(active->inventory(&w)==C::Busy);++reentries;reenter=true;}return retained?int(C::Retained):int(C::Ok);}};}
static A::Guard guard(){return {nullptr,[](void*){return F::onWorker&&F::lease;},[](void*){return epoch;}};}
static void put(uint8_t*p,uint32_t n){Old::word(p,n);}
static Bytes delta(uint64_t gen,Bytes value){Bytes out(8+value.size());put(out.data(),uint32_t(gen));put(out.data()+4,uint32_t(gen>>32));memcpy(out.data()+8,value.data(),value.size());return out;}
static Bytes journal(const Rows&rows,uint64_t generation=7,bool anchor=false){Old::State state{};strcpy(state.owner,LegacyOwner);state.generation=generation;for(auto&r:rows){assert(state.count<16&&r.second.size()<=64);auto&e=state.entries[state.count++];strcpy(e.key,r.first.c_str());e.size=r.second.size();memcpy(e.value,r.second.data(),r.second.size());}Bytes b(Old::Bytes);b.resize(Old::encode(state,6,anchor,b.data()));return b;}
static Bytes anchor2(uint64_t gen=3){Bytes b(132);memcpy(b.data(),"RISCKV2",8);put(b.data()+8,6);put(b.data()+16,uint32_t(gen));put(b.data()+20,uint32_t(gen>>32));strcpy(reinterpret_cast<char*>(b.data()+24),LegacyOwner);put(b.data()+124,132);put(b.data()+128,Old::crc(b.data(),128));return b;}
static void repair(Bytes&b){put(b.data()+b.size()-4,Old::crc(b.data(),b.size()-4));}
static A::Selection selection(const A::Witness&w){A::Selection s{w.kind,w.revision,{}};memcpy(s.fingerprint,w.fingerprint,32);return s;}
static A::Rows sink(Rows&rows){return {&rows,[](void*p,const char*k,const void*v,uint32_t n){auto&r=*static_cast<Rows*>(p);assert(!r.count(k));r[k]=Bytes(static_cast<const uint8_t*>(v),static_cast<const uint8_t*>(v)+n);return int(C::Ok);}};}
static void reset(){assert(iterators.empty());retained=retainWhileOpen=loseWhileOpen=lateGuard=lateInit=epochWhileOpen=reenter=reverseOrder=hideFind=duplicateInfo=false;corruptInfo=0;active=nullptr;epoch=19;F::empty();}
static void seed(){F::nvs["rte00000006"]["original"]={1};F::nvs["rte00000006"]["untouched"]={2};F::nvs["kvj00000006"]["snapshot"]=journal({{"original",{3}},{"journal",{4}}});F::nvs["kvd00000006"]["original"]=delta(2,{5});F::nvs["kvd00000006"]["added"]=delta(9,{6});F::nvs["rnr00000006"]={};}
template<class Fn> static int onWorker(Fn action){
 struct Call {Fn action;J job;explicit Call(Fn fn):action(fn),job([](void*p,uint32_t,const void*,uint32_t)->J::Result{return {static_cast<Call*>(p)->action(),0,0};},this){}} call(action);
 F::onWorker=false;const auto before=F::nvsTotal+F::fsTotal;uint64_t token=0;J::Result result{};
 assert(call.job.begin(1,nullptr,0,&token)==RISC_STORAGE_WORK_OK);
 for(unsigned i=0;i<4;++i)assert(call.job.collect(token,&result)==RISC_STORAGE_WORK_PENDING);
 assert(F::nvsTotal+F::fsTotal==before);F::onWorker=true;assert(call.job.workerStep());F::onWorker=false;
 assert(call.job.collect(token,&result)==RISC_STORAGE_WORK_OK&&call.job.exitSafe());F::onWorker=true;return result.status;
}
static void maps(){
 for(unsigned mode=0;mode<6;++mode){reset();Rows expected;
  if(mode==0){for(unsigned i=0;i<35;++i)expected["opaque."+std::to_string(i)]=Bytes(i==0?2048:65,uint8_t(i));expected["snapshot"]={1,2,3};F::nvs["rte00000006"]=expected;}
  if(mode==1){seed();expected={{"added",{6}},{"original",{5}},{"journal",{4}},{"untouched",{2}}};}
  if(mode==2){F::nvs["kvj00000006"]["snapshot"]=journal({});}
  if(mode==3){F::nvs["kvd00000006"]["only"]=delta(UINT64_MAX,{8});expected={{"only",{8}}};}
  if(mode==4){expected={{"full",{8}},{"snapshot",{9}}};F::nvs["kvj00000006"]["snapshot"]=journal(expected);}
  if(mode==5){expected={{"edge",Bytes(2048,1)}};F::nvs["kvd00000006"]["edge"]=delta(1,expected.at("edge"));}
  F::nvs["rte00000007"]["foreign"]={3};auto before=F::nvs;F::Fixture f;Hash h;Sources s(6,Owner,LegacyOwner,30000,f.inspector,h.api(),hooks());A::Witness w;assert(onWorker([&]{return s.inventory(&w);})==C::Ok&&s.exitSafe()&&F::nvs==before);
  assert(w.namespaceId==6&&w.kind==Sources::UnanchoredNativeV1&&w.revision==1&&w.root==A::RootState::NeverInitialized);
  auto api=s.sources();Rows actual;assert(onWorker([&]{return api.materialize(api.context,6,selection(w),sink(actual),&w);})==C::Ok&&actual==expected&&F::nvs==before&&s.exitSafe());
  reverseOrder=true;assert(api.recheck(api.context,6,w)==C::Ok);active=&s;reenter=true;assert(api.recheck(api.context,6,w)==C::Ok&&reentries);reenter=false;active=nullptr;
  Hash mapHash;A factory(&s,f.core,guard(),mapHash.api(),api);C::Initialization a;assert(onWorker([&]{return factory.admitImported(selection(w),&a);})==C::Ok);assert(onWorker([&]{return f.core.initializeImportedBaseline(a);})==C::Ok&&f.core.baseline().origin()==C::Origin::ImportedLegacy&&f.core.baseline().archiveObligation()&&!f.core.pendingCount());
  for(auto&r:expected){std::string got;assert(F::get(f.core,r.first.c_str(),&got)==C::Ok&&Bytes(got.begin(),got.end())==r.second);}
  for(const char*space:{"rte00000006","kvj00000006","kvd00000006","rte00000007"}){auto prior=before.find(space);assert((prior==before.end()&&!F::nvs.count(space))||(prior!=before.end()&&F::nvs.at(space)==prior->second));}
  f.store.finishOperation();{F::Fixture reboot;assert(reboot.core.load()==C::Ok&&reboot.core.baseline().origin()==C::Origin::ImportedLegacy&&reboot.core.baseline().archiveObligation()&&!memcmp(reboot.core.baseline().sourceFingerprint(),w.fingerprint,32));for(auto&r:expected){std::string got;assert(F::get(reboot.core,r.first.c_str(),&got)==C::Ok&&Bytes(got.begin(),got.end())==r.second);}reboot.store.finishOperation();}assert(s.inventory(&w)==C::Conflict&&!w.namespaceId);
 }
 puts("Legacy NVS: original opaque rows, full v1 journals, exact protocol overlays, empty full journal and v2 deltas import through real admission/store with every original preserved PASS");
}
static void rejected(){unsigned cases=0;
 for(unsigned mode=0;mode<30;++mode){reset();seed();
  if(mode==0)F::nvs["kvj00000006"]["state2"]=anchor2();
  if(mode==1)F::nvs["kvj00000006"]["snapshot"]=journal({},7,true);
  if(mode==2)F::nvs["kvj00000006"]["unexpected"]={1};
  if(mode==3)F::nvs["rte00000006"]["empty"]={};
  if(mode==4)F::nvs["rte00000006"]["big"]=Bytes(2049,1);
  if(mode==5)F::nvs["kvd00000006"]["zero"]=delta(0,{1});
  if(mode==6)F::nvs["kvd00000006"]["short"]=Bytes(8,1);
  if(mode==7)F::nvs["kvd00000006"]["large"]=Bytes(2057,1);
  if(mode==8)F::nvs["kvd00000006"]["tie"]=delta(9,{99});
  if(mode==9)F::nvs["rnr00000006"]["root"]=Bytes(48,1);
  if(mode==10)F::nvs["rnr00000006"]["next"]=Bytes(24,1);
  if(mode==11){std::filesystem::create_directories(F::rootPath+"/n00000006");std::ofstream(F::rootPath+"/n00000006/.pending")<<"preserve";}
  if(mode==12)duplicateInfo=true;
  if(mode==13)hideFind=true;
  if(mode>=14&&mode<19)corruptInfo=mode-13;
  if(mode==19)F::nvs["kvj00000006"]["snapshot"].back()^=1;
  if(mode==20){auto&b=F::nvs["kvj00000006"]["snapshot"];put(b.data()+8,7);repair(b);}
  if(mode==21){auto&b=F::nvs["kvj00000006"]["snapshot"];b[24]='X';repair(b);}
  if(mode==22){auto&b=F::nvs["kvj00000006"]["snapshot"];memcpy(b.data()+124+84,b.data()+124,16);repair(b);}
  if(mode==23){auto&b=F::nvs["kvj00000006"]["snapshot"];put(b.data()+124+16,65);repair(b);}
  if(mode==24){auto&b=F::nvs["kvj00000006"]["snapshot"];put(b.data()+12,2);repair(b);}
  if(mode==25){auto&b=F::nvs["kvj00000006"]["snapshot"];put(b.data()+120,17);repair(b);}
  if(mode==26){auto&b=F::nvs["kvj00000006"]["snapshot"];memset(b.data()+124,'a',16);repair(b);}
  if(mode==27)F::nvs["kvj00000006"]["snapshot"]=Bytes(127,0);
  if(mode==28){auto&b=F::nvs["kvj00000006"]["state2"];b=anchor2();put(b.data()+120,1);repair(b);}
  if(mode==29){auto&b=F::nvs["kvj00000006"]["state2"];b=anchor2();put(b.data()+124,131);repair(b);}
  auto before=F::nvs;F::Fixture f;Hash h;Sources s(6,Owner,LegacyOwner,30000,f.inspector,h.api(),hooks());A::Witness w;int rc=s.inventory(&w);assert(rc!=C::Ok&&!w.namespaceId&&s.exitSafe()&&F::nvs==before&&F::handles.empty()&&iterators.empty());
  if(mode<=1)assert(rc==C::Conflict&&s.reason()==Sources::Reason::ExactSdBaseRequired);
  ++cases;
 }
 reset();F::Fixture f;Hash h;Sources s(6,Owner,LegacyOwner,30000,f.inspector,h.api(),hooks());A::Witness w;assert(s.inventory(&w)==C::Missing&&s.reason()==Sources::Reason::Absent);
 printf("Legacy NVS: %u malformed/unknown/duplicate/anchored/artifact refusals preserve every source; no SD access or stale anchored reconstruction PASS\n",cases);
}
static void bindings(){
 reset();seed();F::Fixture f;Hash h;Sources s(6,Owner,LegacyOwner,30000,f.inspector,h.api(),hooks());A::Witness w;assert(s.inventory(&w)==C::Ok);auto api=s.sources();const auto original=F::nvs;
 for(unsigned mode=0;mode<5;++mode){F::nvs=original;if(mode==0)F::nvs["rte00000006"]["original"]={8};if(mode==1)F::nvs["kvd00000006"]["original"]=delta(3,{5});if(mode==2){auto&b=F::nvs["kvj00000006"]["snapshot"];b[24+strlen(LegacyOwner)+1]=1;repair(b);}if(mode==3)F::nvs["rte00000006"]["new"]={1};if(mode==4)F::nvs.erase("rnr00000006");assert(api.recheck(api.context,6,w)==C::Conflict);}
 F::nvs=original;uint8_t other[16];memcpy(other,Owner,16);other[0]^=1;Hash h2;Sources changed(6,other,LegacyOwner,30000,f.inspector,h2.api(),hooks());auto otherApi=changed.sources();assert(otherApi.recheck(otherApi.context,6,w)==C::Conflict);
 Rows rows;A::Witness out;assert(api.materialize(api.context,7,selection(w),sink(rows),&out)==C::Invalid&&rows.empty());auto wrong=selection(w);wrong.revision++;assert(api.materialize(api.context,6,wrong,sink(rows),&out)==C::Conflict&&rows.empty());
 h.changeAfterFinish=true;assert(s.inventory(&out)==C::Conflict&&!out.namespaceId);F::nvs=original;
 struct Mutating {Rows rows;};Mutating mut;A::Rows mutate{&mut,[](void*p,const char*k,const void*v,uint32_t n){auto&m=*static_cast<Mutating*>(p);auto result=sink(m.rows);int rc=result.append(result.context,k,v,n);F::nvs["rte00000006"]["original"]={9};return rc;}};
 assert(api.materialize(api.context,6,selection(w),mutate,&out)==C::Conflict&&!out.namespaceId);F::nvs=original;
 Hash mapHash;A factory(&s,f.core,guard(),mapHash.api(),api);C::Initialization a;assert(factory.admitImported(selection(w),&a)==C::Ok);F::nvs["rte00000006"]["original"]={9};assert(f.core.initializeImportedBaseline(a)==C::Conflict&&!f.core.ready()&&F::nvs["rnr00000006"].empty());
 F::nvs=original;C::Initialization later;assert(factory.admitImported(selection(w),&later)==C::Ok);++epoch;assert(f.core.initializeImportedBaseline(later)==C::Stale&&!f.core.ready());
 puts("Legacy NVS: owner/namespace/schema selection, shadowed source bytes, generations/padding/absence bound; mutation during hash/append and before import revalidation reject publication PASS");
}
struct Allocation {std::map<void*,size_t> live;unsigned calls=0,failAt=0;~Allocation(){assert(live.empty());}C::Allocator api(){return {this,[](void*p,size_t n)->void*{auto&a=*static_cast<Allocation*>(p);if(++a.calls==a.failAt)return nullptr;auto*b=malloc(n);assert(b);memset(b,0xa5,n);a.live[b]=n;return b;},[](void*p,void*b){auto&a=*static_cast<Allocation*>(p);for(size_t i=0;i<a.live.at(b);++i)assert(!static_cast<uint8_t*>(b)[i]);a.live.erase(b);free(b);}};}};
static void capacities(){
 for(unsigned n:{0u,20u,21u,22u,Sources::MapMax+1}){reset();F::nvs["rte00000006"]["a"]={1};F::Fixture f;Hash h;Allocation allocation;Sources s(6,Owner,LegacyOwner,n,f.inspector,h.api(),hooks(),allocation.api());A::Witness w;assert(s.inventory(&w)==(!n||n>Sources::MapMax?C::Invalid:n<21?C::NoSpace:C::Ok));}
 for(unsigned failAt=1;failAt<=2;++failAt){reset();seed();F::Fixture f;Hash h;Allocation allocation;allocation.failAt=failAt;Sources s(6,Owner,LegacyOwner,30000,f.inspector,h.api(),hooks(),allocation.api());A::Witness w;assert(s.inventory(&w)==C::NoMemory&&!w.namespaceId&&s.exitSafe());}
 for(unsigned failAt=1;failAt<=6;++failAt){reset();seed();F::Fixture f;Hash h;h.failAt=failAt;Sources s(6,Owner,LegacyOwner,30000,f.inspector,h.api(),hooks());A::Witness w;assert(s.inventory(&w)==C::Io&&!w.namespaceId&&s.exitSafe());}
 puts("Legacy NVS: exact map capacity, maximum value, allocation/hash failures, and wiped temporary source/map memory PASS");
}
static void faults(){unsigned cases=0;
 for(bool materialize:{false,true})for(const char*kind:{"nvs-open","nvs-count","nvs-find","nvs-probe","nvs-read"})for(bool after:{false,true})for(unsigned cut=1;cut<200;++cut){reset();seed();F::Fixture f;Hash h;Sources s(6,Owner,LegacyOwner,30000,f.inspector,h.api(),hooks());A::Witness w;assert(s.inventory(&w)==C::Ok);const auto selected=selection(w);auto api=s.sources();Rows rows;const auto before=F::nvs;F::fault=kind;F::cut=cut;F::after=after;F::armed=true;int rc=materialize?api.materialize(api.context,6,selected,sink(rows),&w):s.inventory(&w);F::armed=false;const bool reached=F::nativeCalls>=cut;assert(F::nvs==before&&s.exitSafe()&&F::handles.empty()&&iterators.empty());if(!reached){assert(rc==C::Ok);break;}assert(rc!=C::Ok&&!w.namespaceId);++cases;}
 printf("Legacy NVS: %u reached inventory/materialization NVS failures reject evidence without writes or leaked handles PASS\n",cases);
}
static void truncations(){unsigned cases=0;
 for(bool v2:{false,true}){const Bytes valid=v2?anchor2():journal({{"one",{1}},{"two",{2}}});for(size_t n=0;n<valid.size();++n){reset();F::nvs["kvj00000006"][v2?"state2":"snapshot"]=Bytes(valid.begin(),valid.begin()+n);const auto before=F::nvs;F::Fixture f;Hash h;Sources s(6,Owner,LegacyOwner,30000,f.inspector,h.api(),hooks());A::Witness w;assert(s.inventory(&w)!=C::Ok&&!w.namespaceId&&s.exitSafe()&&F::nvs==before);++cases;}}
 for(int error:{C::Io,C::NoSpace,C::Retained}){reset();seed();F::Fixture f;Hash h;Sources s(6,Owner,LegacyOwner,30000,f.inspector,h.api(),hooks());A::Witness w;assert(s.inventory(&w)==C::Ok);auto api=s.sources();auto selected=selection(w);A::Rows rows{&error,[](void*p,const char*,const void*,uint32_t){return *static_cast<int*>(p);}};assert(api.materialize(api.context,6,selected,rows,&w)==error&&!w.namespaceId);if(error==C::Retained){assert(s.retained()&&!s.exitSafe());const auto calls=F::nvsTotal;assert(s.inventory(&w)==C::Retained&&F::nvsTotal==calls);}else assert(s.exitSafe());}
 printf("Legacy NVS: all %u truncated v1 journal/v2 anchor lengths and append failures reject evidence; Retained append is sticky PASS\n",cases);
}
static void custody(){
 for(unsigned mode=0;mode<8;++mode){reset();seed();F::Fixture f;Hash h;Sources s(6,Owner,LegacyOwner,30000,f.inspector,h.api(),hooks());A::Witness w;
  if(mode==0)F::onWorker=false;
  if(mode==1)F::initialized=false;
  if(mode==2)epoch=0;
  if(mode==3)lateGuard=true;
  if(mode==4)lateInit=true;
  if(mode==5)retainWhileOpen=true;
  if(mode==6)loseWhileOpen=true;
  if(mode==7)epochWhileOpen=true;
  auto calls=F::nvsTotal;int rc=s.inventory(&w);assert(!w.namespaceId);if(mode<5)assert(F::nvsTotal==calls);
  assert(rc==(mode==0||mode==4?C::Busy:mode==1?C::Io:mode==2?C::Stale:C::Retained));
  if(mode>=5||mode==3){assert(!s.exitSafe());calls=F::nvsTotal;assert(s.inventory(&w)==C::Retained&&F::nvsTotal==calls);}
  F::handles.clear();for(auto*i:iterators)delete i;iterators.clear();F::lease=true;
 }
 reset();seed();F::Fixture f;Hash h;h.retainAfterFinish=true;Sources s(6,Owner,LegacyOwner,30000,f.inspector,h.api(),hooks());A::Witness w;assert(s.inventory(&w)==C::Retained&&!s.exitSafe());F::lease=true;retained=false;
 puts("Legacy NVS: worker/global-lease/epoch/initialization gates, reentry, late callback lease loss and sticky Retained precedence suppress further I/O PASS");
}
#if RISC_NATIVE_ESP_IDF
static void sdk6IteratorErrors(){
 for(unsigned fault=1;fault<=7;++fault){
  reset();seed();F::Fixture f;Hash h;Sources s(6,Owner,LegacyOwner,30000,f.inspector,h.api(),hooks());A::Witness w;
  const auto before=F::nvs;sdk6IteratorFault=fault;assert(s.inventory(&w)==C::Io&&!w.namespaceId);
  sdk6IteratorFault=0;assert(s.exitSafe()&&F::nvs==before&&F::handles.empty()&&iterators.empty());
 }
 reset();F::nvs["rte00000006"]={};F::Fixture f;Hash h;Sources s(6,Owner,LegacyOwner,30000,f.inspector,h.api(),hooks());A::Witness w;
 sdk6IteratorFault=1;assert(s.inventory(&w)==C::Io&&!w.namespaceId);sdk6IteratorFault=0;
 assert(s.exitSafe()&&F::handles.empty()&&iterators.empty());
 puts("SDK6 legacy-source find/next/info errors, including empty namespace, never become EOF/absence PASS");
}
#endif
int main(int argc,char**argv){assert(argc==2);F::rootPath=argv[1];setbuf(stdout,nullptr);
#if RISC_NATIVE_ESP_IDF
sdk6IteratorErrors();
#endif
maps();rejected();bindings();capacities();faults();truncations();custody();reset();printf("Collector host size=%zu; disconnected, no production selection or hardware qualification\n",sizeof(Sources));return 0;}
