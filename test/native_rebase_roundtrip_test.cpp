// Disconnected end-to-end proof. Every credential and medium is synthetic.
#include "native_replica_sd_fixture.h"
#include "native_replica_checked_fixture.h"
#include "experimental/native_replica/CheckedSdReplicaReader.h"
#include <experimental/wifi_reconcile/WifiStage.h>
#include <memory>
using R=RiscExperimental::NativeRebaseCandidate;
using Reader=RiscExperimental::CheckedSdReplicaReader;
using W=RiscWifiExperimental::Stage;
using M=RiscWifiExperimental::Reconcile;
namespace Nf=NativeFixture;
// Retained copied jobs and their callback contexts survive until simulated reset.
std::vector<std::shared_ptr<void>> retainedJobs;
void resetProcess(){retainedJobs.clear();Nf::reset();}

// Native entry points run through the actual copied job; owner polling cannot
// perform file/NVS work. The test's owner/worker flags model admitted custody.
template<class F> int nativeCall(Nf::Fixture&f,F action){
 struct Call {F action;J job;explicit Call(F a):action(a),job([](void*p,uint32_t,const void*,uint32_t)->J::Result{return {static_cast<Call*>(p)->action(),0,0};},this){}};
 auto call=std::make_shared<Call>(action);auto&job=call->job;
 uint64_t token=0;J::Result result{};Nf::onWorker=false;
 const unsigned calls=Nf::nvsTotal+Nf::fsTotal;
 assert(job.begin(1,nullptr,0,&token)==RISC_STORAGE_WORK_OK);
 for(unsigned i=0;i<5;++i)assert(job.collect(token,&result)==RISC_STORAGE_WORK_PENDING);
 assert(Nf::nvsTotal+Nf::fsTotal==calls);
 Nf::onWorker=true;assert(job.workerStep());Nf::onWorker=false;
 assert(job.collect(token,&result)==RISC_STORAGE_WORK_OK);
 assert(job.exitSafe()==(result.status!=C::Retained));
 if(result.status==C::Retained)retainedJobs.push_back(call);
 Nf::onWorker=true;f.store.finishOperation();return result.status;
}
template<class Job> void poll(Sd&sd,Job&job,bool execute=true){
 const unsigned calls=Nf::nvsTotal+Nf::fsTotal;Nf::onWorker=false;
 try{sd.tick(job,execute);}catch(...){Nf::onWorker=true;throw;}
 Nf::onWorker=true;assert(Nf::nvsTotal+Nf::fsTotal==calls);
}
template<class Job> unsigned drain(Sd&sd,Job&job){unsigned steps=0;while(job.state()==Job::State::Running){poll(sd,job);assert(++steps<100000);}return steps;}
uint32_t word(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
std::vector<C::Value> descriptors(const uint8_t*p,uint32_t n,uint32_t count){
 std::vector<C::Value> rows;uint32_t at=0;
 for(uint32_t i=0;i<count;++i){assert(at+20<=n);uint32_t z=word(p+at+16);assert(z&&z<=n-at-20);rows.push_back({reinterpret_cast<const char*>(p+at),p+at+20,z});at+=20+z;}
 assert(at==n);return rows;
}
Bytes mapOf(C&core){C::Snapshot s;assert(core.captureSnapshot(&s)==C::Ok);return Bytes(s.map(),s.map()+s.mapSize());}
Bytes intents(C&core){Bytes out;uint64_t cursor=0;for(unsigned i=0;i<core.pendingCount();++i){uint8_t b[1024]{};uint64_t id=0;uint32_t n=0;assert(core.pending(cursor,&id,b,sizeof b,&n)==C::Ok);for(unsigned j=0;j<8;++j)out.push_back(uint8_t(id>>(8*j)));out.insert(out.end(),b,b+n);C::wipe(b,sizeof b);cursor=id;}return out;}
Bytes nativeRoot(){return Nf::nvs.at("rnr00000006").at("root");}
struct NativeImage {
 decltype(Nf::nvs) metadata;Files banks;
 NativeImage():metadata(Nf::nvs){for(auto&e:std::filesystem::recursive_directory_iterator(Nf::rootPath))if(e.is_regular_file()){std::ifstream in(e.path(),std::ios::binary);banks[std::filesystem::relative(e.path(),Nf::rootPath).string()]=Bytes(std::istreambuf_iterator<char>(in),{});}}
 void restore()const{retainedJobs.clear();Nf::empty();Nf::nvs=metadata;for(auto&e:banks){auto path=std::filesystem::path(Nf::rootPath)/e.first;std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(e.second.data()),e.second.size());assert(out.good());}}
};
struct AppStore {
 Files rows,writes;
 risc_key_value_v1 api(){return {1,sizeof(risc_key_value_v1),this,
  [](void*p,const char*k,void*out,uint32_t cap,uint32_t*n)->int32_t{auto&s=*static_cast<AppStore*>(p);*n=0;auto it=s.rows.find(k);if(it==s.rows.end())return RISC_KEY_VALUE_NOT_FOUND;*n=it->second.size();if(cap<*n)return RISC_KEY_VALUE_BUFFER_SMALL;memcpy(out,it->second.data(),*n);return RISC_KEY_VALUE_OK;},
  [](void*p,const char*k,const void*value,uint32_t n)->int32_t{auto&s=*static_cast<AppStore*>(p);const auto*b=static_cast<const uint8_t*>(value);s.rows[k]=Bytes(b,b+n);s.writes[k]=s.rows[k];return RISC_KEY_VALUE_OK;}};}
 Bytes map()const{Bytes b;for(auto&e:rows){auto at=b.size();b.resize(at+20+e.second.size());memcpy(b.data()+at,e.first.c_str(),e.first.size());put32(b.data()+at+16,e.second.size());memcpy(b.data()+at+20,e.second.data(),e.second.size());}return b;}
};
portable_wifi_credentials credential(const std::string&name,const char*password="synthetic-password"){portable_wifi_credentials c{};assert(name.size()<sizeof c.ssid&&strlen(password)<sizeof c.password);strcpy(c.ssid,name.c_str());strcpy(c.password,password);return c;}
portable_wifi_credentials remoteCredential(unsigned i){return credential("synthetic-remote-"+std::to_string(i));}
AppStore seedProfiles(unsigned count){AppStore s;auto api=s.api();for(unsigned i=0;i<count;++i){auto c=remoteCredential(i);assert(portable_wifi_profile_save(&api,i,&c)==0);}for(unsigned i=0;i<40;++i)s.rows["opaque."+std::to_string(i)]=Bytes(64,uint8_t(i+1));s.writes.clear();return s;}
void appEdit(Nf::Fixture&f,AppStore&local,M::Operation op,unsigned slot,const portable_wifi_credentials&desired,const portable_wifi_credentials*expected=nullptr){
 M::Intent intent{};assert(M::make(op,desired,expected,&intent));local.writes.clear();auto api=local.api();
 if(op==M::Save)assert(portable_wifi_profile_save(&api,slot,&desired)==0);else assert(portable_wifi_profile_forget(&api,slot)==PORTABLE_WIFI_CREDENTIALS_FORGOTTEN);
 std::vector<C::Mutation> changes;for(auto&e:local.writes)changes.push_back({e.first.c_str(),C::Put,e.second.data(),uint32_t(e.second.size())});
 assert(nativeCall(f,[&]{return f.core.apply(f.core.revision(),changes.data(),changes.size(),intent.bytes,M::IntentBytes);})==C::Ok);M::wipe(&intent,sizeof intent);
}
struct Setup {
 Files sd;Bytes pending,oldMap;C::Identity remoteId{};
 Setup(){Nf::empty();Nf::Fixture f;AppStore local=seedProfiles(24);auto initial=local.map();auto rows=descriptors(initial.data(),initial.size(),local.rows.size());auto base=identity(initial,rows.size(),7);
  // Trusted fixture bootstrap only; no creation/migration policy is implemented.
  assert(nativeCall(f,[&]{return f.core.initialize(rows.data(),rows.size(),base);})==C::Ok);
  auto alpha=credential("synthetic-alpha","synthetic-one"),alpha2=credential("synthetic-alpha","synthetic-two"),beta=credential("synthetic-beta"),gamma=credential("synthetic-gamma");
  appEdit(f,local,M::Save,24,alpha);appEdit(f,local,M::Save,25,beta);appEdit(f,local,M::Save,24,alpha2,&alpha);
  auto old=remoteCredential(3),forget=credential(old.ssid,"");appEdit(f,local,M::Forget,3,forget,&old);appEdit(f,local,M::Save,3,gamma);
  forget=credential(beta.ssid,"");appEdit(f,local,M::Forget,25,forget,&beta);appEdit(f,local,M::Save,25,remoteCredential(30));
  M::Intent absent{};auto ghost=credential("synthetic-absent","");assert(M::make(M::Forget,ghost,nullptr,&absent));C::Mutation deletion{"absent",C::Delete,nullptr,0};
  assert(nativeCall(f,[&]{return f.core.apply(f.core.revision(),&deletion,1,absent.bytes,M::IntentBytes);})==C::Ok);M::wipe(&absent,sizeof absent);
  assert(f.core.pendingCount()==8);pending=intents(f.core);oldMap=mapOf(f.core);
  AppStore remote=seedProfiles(32);auto map=remote.map();assert(remote.rows.size()>16&&map.size()>4096);remoteId=identity(map,remote.rows.size(),9);save(sd,map,remote.rows.size(),remoteId);
 }
};
struct Bridge {
 R&candidate;uint64_t view;
 explicit Bridge(R&c):candidate(c),view(c.viewRevision()){}
 static int32_t result(int rc){switch(rc){case C::Ok:return RISC_KEY_VALUE_OK;case C::Missing:return RISC_KEY_VALUE_NOT_FOUND;case C::BufferSmall:return RISC_KEY_VALUE_BUFFER_SMALL;case C::Busy:return RISC_KEY_VALUE_BUSY;default:return RISC_KEY_VALUE_IO;}}
 risc_key_value_v1 api(){return {1,sizeof(risc_key_value_v1),this,[](void*p,const char*k,void*out,uint32_t cap,uint32_t*n)->int32_t{auto&b=*static_cast<Bridge*>(p);return result(b.candidate.get(b.view,k,out,cap,n));},[](void*,const char*,const void*,uint32_t)->int32_t{assert(false);return RISC_KEY_VALUE_CONTEXT;}};}
 W::Target target(){return {this,[](void*p,const W::Batch*b){auto&bridge=*static_cast<Bridge*>(p);assert(b&&b->count<=W::MaxMutations);C::Mutation mutations[W::MaxMutations]{};for(uint32_t i=0;i<b->count;++i)mutations[i]={b->mutations[i].key,C::Put,b->mutations[i].value,b->mutations[i].size};
  R::Batch batch{b->namespaceId,b->viewRevision,b->pendingFrontier,b->originalIntentId,b->originalIntent.bytes,M::IntentBytes,mutations,b->count};int rc=bridge.candidate.stage(batch);C::wipe(mutations,sizeof mutations);
  switch(rc){case C::Ok:return W::CommitOk;case C::Busy:return W::CommitBusy;case C::Stale:return W::CommitStale;case C::NoMemory:case C::NoSpace:return W::CommitCapacity;case C::Conflict:case C::Invalid:return W::CommitConflict;default:return W::CommitIo;}}};}
};
W::Result prepare(R&r,unsigned index,Bridge&bridge,W&stage){M::Intent intent{};uint64_t id=0;uint32_t n=0;assert(r.pending(r.viewRevision(),index,&id,intent.bytes,sizeof intent.bytes,&n)==C::Ok&&n==M::IntentBytes);auto api=bridge.api();assert(stage.begin({&api,bridge.view,r.frontier()},id,intent)==W::Again);M::wipe(&intent,sizeof intent);unsigned steps=0;while(stage.step()==W::Again)assert(++steps<100000);return stage.result();}
void resolve(R&r,C&core,unsigned count=UINT32_MAX){const auto root=nativeRoot(),pending=intents(core);const auto revision=core.revision();unsigned calls=Nf::nvsTotal+Nf::fsTotal;count=std::min(count,r.pendingCount());
 for(unsigned i=0;i<count;++i){Bridge bridge(r);W stage;assert(prepare(r,i,bridge,stage)==W::Ready);if(i>=6)assert(stage.batch()->count==0);assert(stage.commit(bridge.target())==W::Committed&&stage.sensitiveCleared());assert(nativeRoot()==root&&core.revision()==revision&&intents(core)==pending);}
 assert(Nf::nvsTotal+Nf::fsTotal==calls);
}
void acquire(C&core,Sd&sd,Reader::Candidate&candidate){Hash hash;Reader reader(core,hash.api(),sd.guard());unsigned calls=Nf::nvsTotal+Nf::fsTotal;assert(reader.start(sd.volume())==C::Ok);drain(sd,reader);assert(reader.state()==Reader::State::Verified&&reader.take(&candidate)==C::Ok&&reader.exitSafe());assert(Nf::nvsTotal+Nf::fsTotal==calls&&candidate.valid());}
R::Remote remote(const Reader::Candidate&c){return {c.namespaceId(),c.count(),c.mapSize(),c.map(),c.identity()};}
void rebase(Nf::Fixture&f,Files&files){Sd sd(files);Reader::Candidate candidate;acquire(f.core,sd,candidate);R receiver(f.core,6,{},C::heap());assert(receiver.begin(remote(candidate))==C::Ok);candidate.reset();resolve(receiver,f.core);assert(nativeCall(f,[&]{return receiver.finish(receiver.viewRevision(),receiver.frontier());})==C::Ok);}
risc_key_value_v1 coreApi(C&core){return {1,sizeof(risc_key_value_v1),&core,[](void*p,const char*k,void*out,uint32_t cap,uint32_t*n)->int32_t{return Bridge::result(static_cast<C*>(p)->get(k,out,cap,n));},[](void*,const char*,const void*,uint32_t)->int32_t{assert(false);return RISC_KEY_VALUE_CONTEXT;}};}
void expectProfiles(C&core){auto api=coreApi(core);uint32_t count=0;assert(portable_wifi_profile_count(&api,&count)==0&&count==34);std::map<std::string,std::string> actual,expected;
 for(unsigned i=0;i<count;++i){portable_wifi_credentials c{};int rc=portable_wifi_profile_load(&api,i,&c);assert(rc==PORTABLE_WIFI_CREDENTIALS_LOADED||rc==PORTABLE_WIFI_CREDENTIALS_EMPTY);if(!rc){assert(actual.emplace(c.ssid,c.password).second);}portable_wifi_credentials_clear(&c);}
 for(unsigned i=0;i<32;++i)if(i!=3){auto c=remoteCredential(i);expected[c.ssid]=c.password;}
 expected["synthetic-alpha"]="synthetic-two";expected["synthetic-gamma"]="synthetic-password";assert(actual==expected&&actual.size()==33);
 for(unsigned i=0;i<40;++i){std::string k="opaque."+std::to_string(i);uint8_t b[64]{};uint32_t n=0;assert(core.get(k.c_str(),b,sizeof b,&n)==C::Ok&&n==64);for(uint8_t v:b)assert(v==i+1);}
}
void exportAck(Nf::Fixture&f,Files&files){Sd sd(files);Hash hash;T writer(hash.api(),sd.guard());assert(nativeCall(f,[&]{return writer.start(f.core,sd.volume());})==C::Ok);drain(sd,writer);assert(writer.state()==T::State::Verified&&f.core.pendingCount()==8);assert(nativeCall(f,[&]{return writer.acknowledgeNative();})==C::Ok&&writer.exitSafe());assert(!f.core.pendingCount());}
void basicRoundtrip(){Setup s;Bytes finalMap;C::Identity finalId{};uint32_t keyCount=0;
 {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);const auto original=intents(f.core);rebase(f,s.sd);assert(intents(f.core)==original&&f.core.identity().generation==9);expectProfiles(f.core);finalMap=mapOf(f.core);exportAck(f,s.sd);expectProfiles(f.core);finalId=f.core.identity();assert(finalId.generation==10);C::Snapshot snap;assert(f.core.captureSnapshot(&snap)==C::Ok);keyCount=snap.count();assert(keyCount>16);}
 {Nf::Fixture reboot;assert(nativeCall(reboot,[&]{return reboot.core.load();})==C::Ok);assert(!reboot.core.pendingCount()&&mapOf(reboot.core)==finalMap);expectProfiles(reboot.core);}
 // Model a complete native erase only AFTER the full dataset reached checked
 // SD publication and durable native acknowledgement. No native-only survival.
 Nf::empty();assert(Nf::nvs.empty()&&std::filesystem::is_empty(Nf::rootPath));
 {Nf::Fixture erased;assert(nativeCall(erased,[&]{return erased.core.load();})==C::Missing);Sd sd(s.sd);Reader::Candidate candidate;acquire(erased.core,sd,candidate);assert(candidate.count()==keyCount&&candidate.mapSize()==finalMap.size()&&!memcmp(candidate.map(),finalMap.data(),finalMap.size()));
  // Explicit test-owned restore authority. Initialize the FULL verified map in
  // one checked native publication; never install an empty/default placeholder.
  auto rows=descriptors(candidate.map(),candidate.mapSize(),candidate.count());auto id=candidate.identity();assert(id.generation==finalId.generation&&!memcmp(id.digest,finalId.digest,32));assert(nativeCall(erased,[&]{return erased.core.initialize(rows.data(),rows.size(),id);})==C::Ok);candidate.reset();expectProfiles(erased.core);assert(mapOf(erased.core)==finalMap&&!erased.core.pendingCount());}
 {Nf::Fixture reboot;assert(nativeCall(reboot,[&]{return reboot.core.load();})==C::Ok);expectProfiles(reboot.core);assert(mapOf(reboot.core)==finalMap&&!reboot.core.pendingCount());}
 printf("checked full chain: %u keys, 33 surviving networks, 8 offline Save/update/Forget/unchanged intents, checked SD ack, full native erase and exact verified restore PASS\n",keyCount);
}
void stagedRestarts(){Setup s;NativeImage before;unsigned cases=0;
 for(unsigned cut=0;cut<=8;++cut){before.restore();Files files=s.sd;{Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Sd sd(files);Reader::Candidate candidate;acquire(f.core,sd,candidate);R r(f.core,6,{},C::heap());assert(r.begin(remote(candidate))==C::Ok);resolve(r,f.core,cut);if(cut<8)assert(r.finish(r.viewRevision(),r.frontier())==C::Conflict);assert(r.abandon()==C::Ok&&mapOf(f.core)==s.oldMap&&intents(f.core)==s.pending);}
  {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);assert(intents(f.core)==s.pending&&mapOf(f.core)==s.oldMap);rebase(f,files);expectProfiles(f.core);exportAck(f,files);}++cases;}
 printf("checked full chain: %u staged-prefix abandon/reboot boundaries retain original intents and successfully re-resolve/export PASS\n",cases);
}
void admissionAndConflict(){Setup s;NativeImage before;unsigned cases=0;
 // Correctly hashed, fully verified foreign/rollback/divergent sources do not
 // acquire authority from validity or generation. A bad page never yields bytes.
 for(unsigned mode=0;mode<7;++mode){before.restore();Files files=s.sd;Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);const auto root=nativeRoot();
  if(mode==0){files.at(pagePath(s.remoteId,0))[24]^=1;Sd sd(files);Hash h;Reader reader(f.core,h.api(),sd.guard());assert(reader.start(sd.volume())==C::Ok);drain(sd,reader);Reader::Candidate c;assert(reader.state()==Reader::State::Failed&&reader.take(&c)==C::Stale&&!c.valid());}
  else {if(mode<=3){AppStore remoteMap=seedProfiles(32);auto bytes=remoteMap.map();auto id=identity(bytes,remoteMap.rows.size(),mode==2?6:mode==3?7:99);if(mode==1)id.lineage[0]^=1;files.clear();save(files,bytes,remoteMap.rows.size(),id);}
   if(mode==4){AppStore m=seedProfiles(32);auto api=m.api();auto c=credential("synthetic-alpha","synthetic-conflict");assert(portable_wifi_profile_save(&api,32,&c)==0);auto b=m.map();files.clear();save(files,b,m.rows.size(),identity(b,m.rows.size(),9));}
   Sd sd(files);Reader::Candidate c;acquire(f.core,sd,c);R::Limits lim{};
   if(mode==5){C::Snapshot snap;assert(f.core.captureSnapshot(&snap)==C::Ok);lim.nativeImageBytes=C::Header+c.mapSize()+4;}
   if(mode==6){C::Snapshot snap;assert(f.core.captureSnapshot(&snap)==C::Ok);lim.nativeImageBytes=C::Header+c.mapSize()+snap.imageSize()-C::Header-snap.mapSize()-4+4;}
   R r(f.core,6,lim,C::heap());int rc=r.begin(remote(c));
   if(mode<=3)assert(rc==C::Conflict);else if(mode==5)assert(rc==C::NoSpace);else {assert(rc==C::Ok);Bridge bridge(r);W stage;auto ready=prepare(r,0,bridge,stage);if(mode==4)assert(ready==W::Conflict);else assert(ready==W::Ready&&stage.commit(bridge.target())==W::Capacity);assert(r.resolvedCount()==0);}
  }
  assert(nativeRoot()==root&&mapOf(f.core)==s.oldMap&&intents(f.core)==s.pending);++cases;
 }
 // Stale native frontier refuses already prepared semantic results.
 before.restore();{Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Sd sd(s.sd);Reader::Candidate c;acquire(f.core,sd,c);R r(f.core,6,{},C::heap());assert(r.begin(remote(c))==C::Ok);Bridge bridge(r);W stage;assert(prepare(r,0,bridge,stage)==W::Ready);C::Mutation change{"concurrent",C::Put,"kept",4};M::Intent i{};auto absent=credential("synthetic-extra-absent","");assert(M::make(M::Forget,absent,nullptr,&i));assert(nativeCall(f,[&]{return f.core.apply(f.core.revision(),&change,1,i.bytes,sizeof i.bytes);})==C::Ok);assert(stage.commit(bridge.target())==W::Stale&&f.core.pendingCount()==9);assert(get(f.core,"concurrent")=="kept");}
 printf("checked full chain: %u verified corruption/lineage/rollback/divergence/schema/capacity refusals plus concurrent frontier preserve native state PASS\n",cases);
}


void readerFaults(){Setup s;NativeImage initial;unsigned calls=0,cases=0;
 {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Sd sd(s.sd);Reader::Candidate c;acquire(f.core,sd,c);calls=sd.io;}
 for(bool crash:{false,true})for(bool after:{false,true})for(unsigned cut=1;cut<=calls;++cut){initial.restore();Files files=s.sd;
  {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);auto root=nativeRoot();Sd sd(files);Hash hash;Reader reader(f.core,hash.api(),sd.guard());assert(reader.start(sd.volume())==C::Ok);sd.fault=cut;sd.after=after;sd.crash=crash;
   try{drain(sd,reader);}catch(const Sd::Cut&){}assert(sd.hit&&intents(f.core)==s.pending&&nativeRoot()==root&&sd.mutations==0);Reader::Candidate c;if(reader.state()==Reader::State::Verified){assert(!crash);assert(reader.take(&c)==C::Ok&&c.valid()&&c.identity().generation==9);AppStore expected=seedProfiles(32);auto bytes=expected.map();assert(c.mapSize()==bytes.size()&&!memcmp(c.map(),bytes.data(),bytes.size()));}else assert(reader.take(&c)==C::Stale&&!c.valid());
   if(reader.state()==Reader::State::Retained){unsigned io=sd.io;reader.cancel();for(unsigned i=0;i<3;++i)poll(sd,reader,false);assert(!reader.exitSafe()&&sd.io==io);}
  }
  resetProcess();{Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);rebase(f,files);exportAck(f,files);expectProfiles(f.core);}++cases;
 }
 initial.restore();Files files=s.sd;files[std::string(Index)+".risc-replace"]=files.at(Index);files.erase(Index);
 {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);rebase(f,files);exportAck(f,files);expectProfiles(f.core);}
 printf("checked full chain: %u reader returned-error/power-cut cases expose only verified bytes and no native mutation; backup-only index and fresh acquisition recover PASS\n",cases);
}
void partitionCapacity(){Setup s;Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Sd sd(s.sd);Reader::Candidate candidate;acquire(f.core,sd,candidate);R r(f.core,6,{},C::heap());assert(r.begin(remote(candidate))==C::Ok);resolve(r,f.core);auto root=nativeRoot();
 Nf::otherUsed=0x80000-32768;assert(nativeCall(f,[&]{return r.finish(r.viewRevision(),r.frontier());})==C::NoSpace);assert(r.held()&&r.resolvedCount()==8&&nativeRoot()==root&&intents(f.core)==s.pending);Nf::otherUsed=0;
 assert(nativeCall(f,[&]{return r.finish(r.viewRevision(),r.frontier());})==C::Ok);exportAck(f,s.sd);expectProfiles(f.core);
 puts("checked full chain: actual AppData partition/recovery-reserve capacity refusal preserves held resolved candidate and permits explicit finish retry PASS");
}
void writerFaults(){Setup s;{Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);rebase(f,s.sd);}
 NativeImage merged;unsigned calls=0,steps=0,cases=0;
 {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Files files=s.sd;Sd sd(files);Hash hash;T writer(hash.api(),sd.guard());assert(nativeCall(f,[&]{return writer.start(f.core,sd.volume());})==C::Ok);steps=drain(sd,writer);assert(writer.state()==T::State::Verified);calls=sd.io;writer.cancel();}
 for(bool crash:{false,true})for(bool after:{false,true})for(unsigned cut=1;cut<=calls;++cut){merged.restore();Files files=s.sd;
  {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);auto root=nativeRoot();Sd sd(files);Hash hash;T writer(hash.api(),sd.guard());assert(nativeCall(f,[&]{return writer.start(f.core,sd.volume());})==C::Ok);sd.fault=cut;sd.after=after;sd.crash=crash;
   try{drain(sd,writer);}catch(const Sd::Cut&){}assert(sd.hit&&intents(f.core)==s.pending&&nativeRoot()==root);expectProfiles(f.core);
   if(writer.state()==T::State::Retained){unsigned io=sd.io;writer.cancel();for(unsigned i=0;i<3;++i)poll(sd,writer,false);assert(!writer.exitSafe()&&sd.io==io&&writer.acknowledgeNative()==C::Stale);}
  }
  // Destruction above models whole-process reset, never retained quiescence.
  resetProcess();{Nf::Fixture reboot;assert(nativeCall(reboot,[&]{return reboot.core.load();})==C::Ok);assert(intents(reboot.core)==s.pending);expectProfiles(reboot.core);exportAck(reboot,files);expectProfiles(reboot.core);}++cases;
 }
 printf("checked full chain: %u reached SD returned-error/power-cut boundaries recover exact captured map through actual native acknowledgement PASS\n",cases);
 unsigned cancellations=0;
 for(unsigned mode=0;mode<3;++mode)for(unsigned cut=0;cut<=steps;++cut){merged.restore();Files files=s.sd;
  {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Sd sd(files);Hash hash;T writer(hash.api(),sd.guard());assert(nativeCall(f,[&]{return writer.start(f.core,sd.volume());})==C::Ok);for(unsigned i=0;i<cut&&writer.state()==T::State::Running;++i)poll(sd,writer);
   if(mode==0){writer.cancel();drain(sd,writer);assert(writer.state()==T::State::Cancelled&&writer.exitSafe());}
   if(mode==1){sd.lease=false;if(writer.state()==T::State::Verified)assert(nativeCall(f,[&]{return writer.acknowledgeNative();})==C::Conflict);else drain(sd,writer);assert(writer.state()==T::State::Failed&&writer.exitSafe());}
   // mode 2 is a process reset at every owner/worker handoff, including Verified.
   assert(intents(f.core)==s.pending);expectProfiles(f.core);
  }
  resetProcess();{Nf::Fixture reboot;assert(nativeCall(reboot,[&]{return reboot.core.load();})==C::Ok);exportAck(reboot,files);expectProfiles(reboot.core);}++cancellations;
 }
 printf("checked full chain: %u SD cancel/lease-loss/restart handoffs preserve every native intent until verified acknowledgement PASS\n",cancellations);
}
void nativePublicationFaults(){Setup s;NativeImage initial;Bytes complete;
 {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);rebase(f,s.sd);complete=mapOf(f.core);}
 NativeImage merged;unsigned cases=0,retentions=0,uncertain=0;
 // This exercises actual file and NVS errors both at native rebase finish and
 // at final native acknowledgement, rather than substituting a root model.
 for(bool acknowledge:{false,true})for(const char*kind:{"nvs-open","nvs-probe","nvs-read","nvs-set","nvs-commit","write","partial","read","sync","close","rename-before","rename-after"})for(bool after:{false,true})for(unsigned cut=1;cut<200;++cut){
  if(acknowledge)merged.restore();else initial.restore();Files files=s.sd;bool reached=false;
  {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Sd sd(files);Hash hash;T writer(hash.api(),sd.guard());Reader::Candidate candidate;R receiver(f.core,6,{},C::heap());
   if(acknowledge){assert(nativeCall(f,[&]{return writer.start(f.core,sd.volume());})==C::Ok);drain(sd,writer);assert(writer.state()==T::State::Verified);}
   else {acquire(f.core,sd,candidate);assert(receiver.begin(remote(candidate))==C::Ok);resolve(receiver,f.core);}
   Nf::fault=kind;Nf::cut=cut;Nf::after=after;Nf::nativeCalls=Nf::fsCalls=0;Nf::armed=true;
   int rc=nativeCall(f,[&]{return acknowledge?writer.acknowledgeNative():receiver.finish(receiver.viewRevision(),receiver.frontier());});Nf::armed=false;reached=Nf::nativeCalls>=cut||Nf::fsCalls>=cut;
   assert(rc==C::Ok||rc==C::Io||rc==C::NoSpace||rc==C::Uncertain||rc==C::Retained);
   if(rc==C::Uncertain){assert(!f.core.ready());++uncertain;}
   if(rc==C::Retained){++retentions;const auto metadata=Nf::nvs;const unsigned calls=Nf::nvsTotal+Nf::fsTotal;assert(!f.store.exitSafe()&&!f.files.exitSafe());
    if(acknowledge){writer.cancel();assert(writer.state()==T::State::Retained&&!writer.exitSafe()&&writer.acknowledgeNative()==C::Stale);}
    else {assert(receiver.abandon()==C::Retained&&receiver.begin(remote(candidate))==C::Retained&&receiver.finish(1,1)==C::Retained);}
    auto backend=f.store.backend();uint64_t object=1;assert(backend.reserve(backend.context,C::Header+4,&object)==C::Retained&&object==0);f.store.finishOperation();assert(Nf::nvs==metadata&&Nf::nvsTotal+Nf::fsTotal==calls);
   }
  }
  resetProcess();{Nf::Fixture reboot;assert(nativeCall(reboot,[&]{return reboot.core.load();})==C::Ok);auto map=mapOf(reboot.core);assert(map==s.oldMap||map==complete);
   if(acknowledge){assert(map==complete);assert(reboot.core.pendingCount()==0||intents(reboot.core)==s.pending);if(reboot.core.pendingCount())exportAck(reboot,files);}
   else {assert(intents(reboot.core)==s.pending);if(map==s.oldMap)rebase(reboot,files);expectProfiles(reboot.core);exportAck(reboot,files);}
   expectProfiles(reboot.core);assert(!reboot.core.pendingCount());
  }
  if(!reached)break;
  ++cases;
 }
 printf("checked full chain: %u actual native finish/ack faults, %u retained and %u uncertain outcomes, complete old/new reload and final recovery PASS\n",cases,retentions,uncertain);
}
void acknowledgedFrontier(){Setup s;{Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);rebase(f,s.sd);}
 NativeImage merged;
 // A later native operation survives a still-live captured acknowledgement.
 {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);Sd sd(s.sd);Hash hash;T writer(hash.api(),sd.guard());assert(nativeCall(f,[&]{return writer.start(f.core,sd.volume());})==C::Ok);drain(sd,writer);assert(writer.state()==T::State::Verified);
  M::Intent intent{};auto absent=credential("synthetic-later-absent","");assert(M::make(M::Forget,absent,nullptr,&intent));C::Mutation change{"late.local",C::Put,"kept",4};assert(nativeCall(f,[&]{return f.core.apply(f.core.revision(),&change,1,intent.bytes,sizeof intent.bytes);})==C::Ok);assert(f.core.pendingCount()==9);
  assert(nativeCall(f,[&]{return writer.acknowledgeNative();})==C::Ok&&f.core.pendingCount()==1&&get(f.core,"late.local")=="kept");expectProfiles(f.core);}
 // Restart plus a newer native edit makes the deterministic replay differ.
 // The generic writer must refuse, and semantic replay may conflict on an
 // intermediate Save that was superseded in the already-published checkpoint.
 // This is an explicit remaining durable-prefix/coordinator gate.
 merged.restore();{Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);M::Intent i{};auto absent=credential("synthetic-later-absent","");assert(M::make(M::Forget,absent,nullptr,&i));C::Mutation change{"late.local",C::Put,"kept",4};assert(nativeCall(f,[&]{return f.core.apply(f.core.revision(),&change,1,i.bytes,sizeof i.bytes);})==C::Ok);}
 {Nf::Fixture f;assert(nativeCall(f,[&]{return f.core.load();})==C::Ok);const auto pending=intents(f.core),root=nativeRoot();Sd sd(s.sd);Hash hash;T writer(hash.api(),sd.guard());assert(nativeCall(f,[&]{return writer.start(f.core,sd.volume());})==C::Ok);drain(sd,writer);assert(writer.state()==T::State::Failed&&writer.reason()==T::Reason::IdentityConflict&&sd.mutations==0);
  Reader::Candidate c;acquire(f.core,sd,c);R receiver(f.core,6,{},C::heap());assert(receiver.begin(remote(c))==C::Ok);Bridge bridge(receiver);W stage;assert(prepare(receiver,0,bridge,stage)==W::Conflict);assert(intents(f.core)==pending&&nativeRoot()==root&&f.core.pendingCount()==9&&get(f.core,"late.local")=="kept");expectProfiles(f.core);}
 puts("checked full chain: live captured ack retains later edit; post-restart divergent replay and superseded intermediate intent fail closed (durable-prefix recovery remains outside proof) PASS");
}
int main(int argc,char**argv){assert(argc==2);setbuf(stdout,nullptr);Nf::rootPath=argv[1];std::filesystem::create_directories(Nf::rootPath);basicRoundtrip();stagedRestarts();admissionAndConflict();readerFaults();partitionCapacity();writerFaults();nativePublicationFaults();acknowledgedFrontier();assert(Nf::handles.empty());puts("DISCONNECTED HOST PROOF ONLY: explicit test restore authority; no creation/migration, product coordinator, port, SDK, UI, .90 activation or hardware qualification");}
