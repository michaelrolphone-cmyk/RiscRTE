// Integration against System Apps' actual frozen Stage/codec, supplied explicitly.
#include "native_rebase_test_support.h"
#include <experimental/wifi_reconcile/WifiStage.h>
using S=RiscWifiExperimental::Stage;using M=RiscWifiExperimental::Reconcile;
struct AppStore {
 std::map<std::string,Bytes> rows,writes;
 risc_key_value_v1 api(){return {1,sizeof(risc_key_value_v1),this,
  [](void*p,const char*key,void*out,uint32_t cap,uint32_t*n)->int32_t{auto&s=*static_cast<AppStore*>(p);*n=0;auto it=s.rows.find(key);if(it==s.rows.end())return RISC_KEY_VALUE_NOT_FOUND;*n=it->second.size();if(cap<*n)return RISC_KEY_VALUE_BUFFER_SMALL;memcpy(out,it->second.data(),*n);return RISC_KEY_VALUE_OK;},
  [](void*p,const char*key,const void*value,uint32_t n)->int32_t{auto&s=*static_cast<AppStore*>(p);const auto*b=static_cast<const uint8_t*>(value);s.rows[key]=Bytes(b,b+n);s.writes[key]=s.rows[key];return RISC_KEY_VALUE_OK;}};}
 Bytes map()const{Bytes bytes;for(const auto&row:rows)append(bytes,row.first.c_str(),row.second.data(),row.second.size());return bytes;}
};
portable_wifi_credentials credential(const char*ssid,const char*password="synthetic-password"){portable_wifi_credentials c{};assert(strlen(ssid)<sizeof c.ssid&&strlen(password)<sizeof c.password);strcpy(c.ssid,ssid);strcpy(c.password,password);return c;}
AppStore seed(unsigned n){AppStore s;auto api=s.api();for(unsigned i=0;i<n;++i){char name[33];snprintf(name,sizeof name,"synthetic-remote-%u",i);auto c=credential(name);assert(portable_wifi_profile_save(&api,i,&c)==0);}s.writes.clear();return s;}
M::Intent applyApp(C&core,AppStore&local,M::Operation operation,unsigned slot,const portable_wifi_credentials&desired,const portable_wifi_credentials*expected=nullptr){
 M::Intent intent{};assert(M::make(operation,desired,expected,&intent));local.writes.clear();auto api=local.api();
 if(operation==M::Save)assert(portable_wifi_profile_save(&api,slot,&desired)==0);else assert(portable_wifi_profile_forget(&api,slot)==PORTABLE_WIFI_CREDENTIALS_FORGOTTEN);
 std::vector<C::Mutation> changes;for(auto&entry:local.writes)changes.push_back({entry.first.c_str(),C::Put,entry.second.data(),uint32_t(entry.second.size())});
 assert(core.apply(core.revision(),changes.data(),changes.size(),intent.bytes,M::IntentBytes)==C::Ok);return intent;
}
struct Bridge {
 R&candidate;uint64_t view;
 explicit Bridge(R&c):candidate(c),view(c.viewRevision()){}
 static int32_t readResult(int rc){switch(rc){case C::Ok:return RISC_KEY_VALUE_OK;case C::Missing:return RISC_KEY_VALUE_NOT_FOUND;case C::BufferSmall:return RISC_KEY_VALUE_BUFFER_SMALL;case C::Busy:return RISC_KEY_VALUE_BUSY;default:return RISC_KEY_VALUE_IO;}}
 risc_key_value_v1 api(){return {1,sizeof(risc_key_value_v1),this,
  [](void*p,const char*k,void*out,uint32_t cap,uint32_t*n)->int32_t{auto&b=*static_cast<Bridge*>(p);return readResult(b.candidate.get(b.view,k,out,cap,n));},
  [](void*,const char*,const void*,uint32_t)->int32_t{assert(false&&"Stage must never write its source");return RISC_KEY_VALUE_CONTEXT;}};}
 S::Target target(){return {this,[](void*p,const S::Batch*b){auto&bridge=*static_cast<Bridge*>(p);assert(b&&b->count<=S::MaxMutations);C::Mutation mutations[S::MaxMutations]{};
  for(uint32_t i=0;i<b->count;++i)mutations[i]={b->mutations[i].key,C::Put,b->mutations[i].value,b->mutations[i].size};
  const R::Batch batch{b->namespaceId,b->viewRevision,b->pendingFrontier,b->originalIntentId,b->originalIntent.bytes,M::IntentBytes,mutations,b->count};
  const int rc=bridge.candidate.stage(batch);C::wipe(mutations,sizeof mutations);
  switch(rc){case C::Ok:return S::CommitOk;case C::Busy:return S::CommitBusy;case C::Stale:return S::CommitStale;case C::NoMemory:case C::NoSpace:return S::CommitCapacity;case C::Conflict:case C::Invalid:return S::CommitConflict;default:return S::CommitIo;}
 }};}
};
S::Result prepare(R&candidate,uint64_t id,const M::Intent&intent,Bridge&bridge,S&stage){auto api=bridge.api();assert(stage.begin({&api,bridge.view,candidate.frontier()},id,intent)==S::Again);unsigned steps=0;while(stage.step()==S::Again)assert(++steps<100000);return stage.result();}
risc_key_value_v1 coreApi(C&core){return {1,sizeof(risc_key_value_v1),&core,[](void*p,const char*k,void*out,uint32_t cap,uint32_t*n)->int32_t{return Bridge::readResult(static_cast<C*>(p)->get(k,out,cap,n));},[](void*,const char*,const void*,uint32_t)->int32_t{return RISC_KEY_VALUE_CONTEXT;}};}
void expect(C&core,unsigned slot,const portable_wifi_credentials&value){auto api=coreApi(core);portable_wifi_credentials actual{};assert(portable_wifi_profile_load(&api,slot,&actual)==0);assert(!strcmp(actual.ssid,value.ssid)&&!strcmp(actual.password,value.password));portable_wifi_credentials_clear(&actual);}
struct Scenario {
 Disk disk;Alloc a,ra;C core;AppStore local,sd;std::vector<M::Intent> intents;Bytes map;portable_wifi_credentials alpha1=credential("synthetic-alpha","synthetic-one"),alpha2=credential("synthetic-alpha","synthetic-two"),beta=credential("synthetic-beta"),gamma=credential("synthetic-gamma");
 Scenario():core(6,disk.backend(),coreLimits(),a.api()),sd(seed(5)){
  assert(core.initialize(nullptr,0,{})==C::Ok);intents.push_back(applyApp(core,local,M::Save,0,alpha1));intents.push_back(applyApp(core,local,M::Save,1,beta));intents.push_back(applyApp(core,local,M::Save,0,alpha2,&alpha1));
  auto forget=credential(beta.ssid,"");intents.push_back(applyApp(core,local,M::Forget,1,forget,&beta));intents.push_back(applyApp(core,local,M::Save,1,gamma));
  intents.push_back(applyApp(core,local,M::Save,2,credential("synthetic-remote-3")));
  M::Intent absent{};auto ghost=credential("synthetic-absent","");assert(M::make(M::Forget,ghost,nullptr,&absent));C::Mutation deletion{"absent",C::Delete,nullptr,0};assert(core.apply(core.revision(),&deletion,1,absent.bytes,M::IntentBytes)==C::Ok);intents.push_back(absent);map=sd.map();ra.verifyWipe=true;
 }
};
void multiOperation(){
 Scenario f;const auto root=f.disk.root,original=nativeIntents(f.core);const auto revision=f.core.revision();
 R c(f.core,6,receiverLimits(),f.ra.api());assert(c.begin(remote(f.map,f.sd.rows.size()))==C::Ok);
 for(unsigned i=0;i<f.intents.size();++i){Bridge bridge(c);S stage;assert(prepare(c,i+1,f.intents[i],bridge,stage)==S::Ready);
  const auto*b=stage.batch();assert(b&&b->originalIntentId==i+1);if(i>=5)assert(b->count==0);if(i==0)assert(!strcmp(b->mutations[0].key,"wifi.profiles"));
  f.disk.calls=0;assert(stage.commit(bridge.target())==S::Committed&&stage.sensitiveCleared()&&stage.cancel()==S::Committed);assert(f.disk.calls==0&&f.disk.root==root&&f.core.revision()==revision&&nativeIntents(f.core)==original&&f.core.pendingCount()==7);expect(f.core,0,f.alpha2);expect(f.core,1,f.gamma);
 }
 assert(c.resolvedCount()==7);assert(c.finish(c.viewRevision(),c.frontier())==C::Ok);auto api=coreApi(f.core);uint32_t n=0;assert(portable_wifi_profile_count(&api,&n)==0&&n==7);expect(f.core,5,f.alpha2);expect(f.core,6,f.gamma);for(unsigned i=0;i<5;++i){char name[33];snprintf(name,sizeof name,"synthetic-remote-%u",i);expect(f.core,i,credential(name));}
 assert(nativeIntents(f.core)==original&&f.core.pendingCount()==7);
 C reboot(6,f.disk.backend(),coreLimits(),f.a.api());assert(reboot.load()==C::Ok&&nativeIntents(reboot)==original);expect(reboot,5,f.alpha2);
 puts("actual Wi-Fi Stage + native core: seven ordered Save/update/Forget/reuse/unchanged/absent intents, offline slot remapping, full remote count retention, no source writes or native publication before finish PASS");
}
void stageFailures(){
 for(unsigned mode=0;mode<5;++mode){Scenario f;const auto root=f.disk.root,original=nativeIntents(f.core);auto limits=receiverLimits();
  if(mode==2){C::Snapshot snapshot;assert(f.core.captureSnapshot(&snapshot)==C::Ok);limits.nativeImageBytes=C::Header+f.map.size()+snapshot.imageSize()-C::Header-snapshot.mapSize()-4+4;}
  R c(f.core,6,limits,f.ra.api());assert(c.begin(remote(f.map,f.sd.rows.size()))==C::Ok);Bridge bridge(c);S stage;assert(prepare(c,1,f.intents[0],bridge,stage)==S::Ready);
  if(mode==0){assert(stage.cancel()==S::Cancelled&&stage.sensitiveCleared());assert(c.abandon()==C::Ok&&f.disk.root==root);}
  else if(mode==1){f.ra.fail=f.ra.calls+1;assert(stage.commit(bridge.target())==S::Capacity&&stage.sensitiveCleared()&&c.resolvedCount()==0&&f.disk.root==root);f.ra.fail=0;}
  else if(mode==2){assert(stage.commit(bridge.target())==S::Capacity&&stage.sensitiveCleared()&&c.resolvedCount()==0&&f.disk.root==root);}
  else if(mode==3){assert(put(f.core,"concurrent","kept","extra-intent")==C::Ok);assert(stage.commit(bridge.target())==S::Stale&&stage.sensitiveCleared()&&c.resolvedCount()==0&&f.core.pendingCount()==8);assert(get(f.core,"concurrent")=="kept");}
  else{assert(c.abandon()==C::Ok);assert(stage.commit(bridge.target())==S::Stale&&stage.sensitiveCleared()&&f.disk.root==root);}
  if(mode!=3)assert(nativeIntents(f.core)==original);
 }
 // A schema conflict never reaches the receiver and keeps all native intents.
 {Scenario f;auto other=credential(f.alpha1.ssid,"synthetic-conflicting");auto api=f.sd.api();assert(portable_wifi_profile_save(&api,5,&other)==0);f.map=f.sd.map();const auto root=f.disk.root;R c(f.core,6,receiverLimits(),f.ra.api());assert(c.begin(remote(f.map,f.sd.rows.size()))==C::Ok);Bridge bridge(c);S stage;assert(prepare(c,1,f.intents[0],bridge,stage)==S::Conflict&&stage.sensitiveCleared());assert(c.resolvedCount()==0&&f.disk.root==root&&f.core.pendingCount()==7);}
 puts("actual Wi-Fi Stage + native core: cancel, allocation/byte capacity, concurrent native edit, abandoned view and app-owned logical conflict preserve native replica and pending groups PASS");
}
int main(){multiOperation();stageFailures();puts("INTEGRATION FIXTURE ONLY: checked SD import/authority remains a caller precondition; no product routing, credentials, radio connection or hardware claim");}
