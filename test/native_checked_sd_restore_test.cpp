// Actual checked source/store composition. The renamed original suite supplies
// the deterministic NVS SDK functions, never its test-only initialization path.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#pragma push_macro("main")
#undef main
#define main original_fresh_sources_main
#include "native_nvs_fresh_sources_test.cpp"
#undef main
#pragma pop_macro("main")
#pragma GCC diagnostic pop
#include "native_settings_catalog_fixture.h"
#include "experimental/native_replica/CheckedSdNativeRestore.h"
using Restore=RiscExperimental::CheckedSdNativeRestore;
using Reader=RiscExperimental::CheckedSdReplicaReader;
using Attempt=RiscExperimental::NativeSdAttempt;
using Completion=RiscExperimental::NativeSdCompletion;
struct NativeSd;static NativeSd*activeSd;
struct NativeSd {
 DirectorySd sd;Hash readHash,restoreHash;F::Fixture f;Sources sources;uint64_t epoch=19;
 Attempt attempt;K reader;Restore restore;
 NativeSd(Files&files,const uint8_t*owner=Owner,C::Allocator allocator=C::heap()):sd(files),sources(6,owner,f.inspector,hooks()),attempt({6,55,7,71,sd.volume()},attemptHooks()),reader(f.core,readHash.api(),readerGuard(),allocator),restore(f.core,f.store,sources,restoreDigest(),restoreGuard()){
  F::onWorker=false;activeSd=this;
  sd.native.submit=[](void*p,const void*q,uint32_t n,uint64_t*t){auto&s=*activeSd;assert(s.attempt.rawSubmit(71,q,n));const int rc=static_cast<Sd*>(p)->worker.submit(q,n,t);s.attempt.submitted(71,rc,rc?0:*t);return rc;};
  sd.native.collect=[](void*p,uint64_t t,void*q,uint32_t n,uint32_t*z,int32_t*r){assert(activeSd->attempt.rawCollect(71,t));return static_cast<Sd*>(p)->worker.collect(t,q,n,z,r);};
  sd.native.cancel=[](void*p,uint64_t t){assert(activeSd->attempt.rawCancel(71,t));return static_cast<Sd*>(p)->worker.cancel(t);};
  auto&api=sd.download.base.base;
  api.begin=[](void*p,uint64_t*t){assert(activeSd->attempt.syncAdmission(55));return risc_storage_async_begin(&static_cast<Sd*>(p)->adapter,t);};
  api.submit=[](void*p,uint64_t session,const risc_storage_async_request_v1*q,uint64_t*t){assert(activeSd->attempt.syncAdmission(55));return risc_storage_async_submit(&static_cast<Sd*>(p)->adapter,session,q,t);};
  api.collect=[](void*p,uint64_t session,uint64_t op,risc_storage_async_result_v1*r){assert(activeSd->attempt.syncAdmission(55));return risc_storage_async_collect(&static_cast<Sd*>(p)->adapter,session,op,r);};
  api.end=[](void*p,uint64_t session){assert(activeSd->attempt.syncAdmission(55));return risc_storage_async_end(&static_cast<Sd*>(p)->adapter,session);};
  sd.download.base.submit_package=[](void*p,uint64_t session,const risc_storage_package_request_v1*q,uint64_t*t){assert(activeSd->attempt.syncAdmission(55));return risc_storage_package_submit(&static_cast<Sd*>(p)->adapter,session,q,t);};
  sd.download.base.base.fs.state.observe=[](void*p){assert(!F::onWorker);auto&s=*static_cast<Sd*>(p);return s.retained?int(RISC_STORAGE_STATE_RETAINED):s.available?int(RISC_STORAGE_STATE_READY):int(RISC_STORAGE_STATE_UNAVAILABLE);};
 }
 Restore::Guard restoreGuard(){return {this,[](void*){return !F::onWorker;},[](void*){return F::onWorker;},[](void*){return F::onWorker&&F::lease;},[](void*p){return static_cast<NativeSd*>(p)->epoch;}};}
 Reader::Guard readerGuard(){return {this,[](void*){return F::lease;},[](void*){return F::onWorker&&F::lease;}};}
 Restore::Digest restoreDigest(){auto d=restoreHash.api();return {d.context,d.begin,d.update,d.finish};}
 Attempt::Hooks attemptHooks(){return {this,[](void*){return !F::onWorker;},[](void*,const Attempt::Binding&){return true;},[](void*){return true;},[](void*,const Attempt::Binding&){return bool(F::lease);},[](void*,bool){},[](void*p,uint64_t*out){auto&s=*static_cast<NativeSd*>(p);assert(!F::onWorker);*out=s.epoch;return s.sd.retained?Attempt::Scope::Retained:Attempt::Scope::Healthy;}};}
 void sdStep(){assert(!F::onWorker);sd.executing=true;sd.worker.workerStep();sd.executing=false;}
 void acquire(){const int acquired=attempt.acquire(sd.volume());if(acquired)fprintf(stderr,"acquire rc %d\n",acquired);assert(!acquired);assert(!attempt.refresh());risc_storage_async_result_v1 result{};int rc=RISC_STORAGE_ASYNC_PENDING;while(rc==RISC_STORAGE_ASYNC_PENDING){sdStep();rc=attempt.collect(&result);}assert(!rc);int closed=attempt.close();while(closed==RISC_STORAGE_ASYNC_PENDING||closed==RISC_STORAGE_ASYNC_BUSY){sdStep();closed=attempt.close();}assert(!closed);assert(attempt.state()==Attempt::State::ReadyClosed);}
 void catalog(K::Limits limits={}){
  acquire();const auto nvs=F::nvsTotal,fs=F::fsTotal;assert(reader.prepareRestoreNative(attempt,limits)==C::Ok);
  F::onWorker=true;assert(reader.preparationWorkerStep());F::onWorker=false;assert(reader.collectPreparation()==C::Ok);
  while(reader.state()==K::State::Running){reader.step();sdStep();}
  assert(reader.state()==K::State::Verified);assert(F::nvsTotal==nvs&&F::fsTotal==fs);
 }
 void candidate(Reader::Candidate*out){catalog();assert(reader.takeRestoreCandidate(out)==C::Ok&&reader.exitSafe());}
 int execute(bool cancel=false){
  Reader::Candidate c;candidate(&c);assert(!restore.begin(attempt,c)&&!c.valid());
  const auto nvs=F::nvsTotal,fs=F::fsTotal,io=sd.io;Completion::Result result{};
  for(unsigned i=0;i<10;++i)assert(restore.collect(&result)==C::Busy);
  assert(F::nvsTotal==nvs&&F::fsTotal==fs);if(cancel)restore.cancel();
  F::onWorker=true;assert(restore.workerStep());F::onWorker=false;assert(sd.io==io);
  int rc=restore.collect(&result);if(rc==C::Ok){assert(!attempt.release()&&attempt.exitSafe()&&restore.exitSafe());return result.status;}return rc;
 }
};
static Files checkpoint(Bytes*map=nullptr,C::Identity*id=nullptr){
 // Deliberately unsorted, full opaque bytes including more than two profile slots.
 Bytes m;for(unsigned i=0;i<30;++i){const unsigned slot=29-i;char key[16];snprintf(key,sizeof key,"w%08x.a0",slot);const auto at=m.size();m.resize(at+84);strcpy(reinterpret_cast<char*>(m.data()+at),key);put32(m.data()+at+16,64);for(unsigned j=0;j<64;++j)m[at+20+j]=uint8_t(slot+j);}
 const auto at=m.size();m.resize(at+2068);strcpy(reinterpret_cast<char*>(m.data()+at),"opaque");put32(m.data()+at+16,2048);for(unsigned i=0;i<2048;++i)m[at+20+i]=uint8_t(i);
 auto identity_=identity(m,31,19);Files files;save(files,m,31,identity_);if(map)*map=m;if(id)*id=identity_;return files;
}
static void exactMap(C&core,const Bytes&map,const C::Identity&id){
 assert(core.ready()&&core.revision()==1&&core.pendingCount()==0&&core.identity().known&&core.identity().generation==id.generation&&!memcmp(core.identity().lineage,id.lineage,16)&&!memcmp(core.identity().digest,id.digest,32));C::Snapshot snap;assert(core.captureSnapshot(&snap)==0&&snap.count()==31&&snap.mapSize()==map.size()&&!memcmp(snap.map(),map.data(),map.size()));
}
static void roundtrip(){
 reset();Bytes map;C::Identity id;auto sd=checkpoint(&map,&id);const auto original=sd;
 {NativeSd s(sd);assert(s.execute()==C::Ok);exactMap(s.f.core,map,id);assert(sd==original);F::onWorker=true;}
 F::reset();{F::Fixture reboot;assert(reboot.core.load()==C::Ok);exactMap(reboot.core,map,id);assert(F::put(reboot.core,"later","explicit")==0);reboot.store.finishOperation();}
 F::reset();const auto native=F::nvs;{NativeSd s(sd);assert(s.execute()==C::Conflict&&F::nvs==native&&sd==original);F::onWorker=true;}
 puts("Checked SD restore: complete unsorted opaque map, 30 profile records, exact identity, reboot and no selected-native overwrite PASS");
}
static void conflicts(){
 unsigned count=0;for(unsigned mode=0;mode<9;++mode){reset();auto sd=checkpoint();
  if(mode<3)F::nvs[std::string(mode==0?"rte":mode==1?"kvj":"kvd")+"00000006"]["valuable"]={1,2,3};
  if(mode==3)F::nvs["rnr00000006"]["unknown"]={4};
  if(mode==4){Bytes root_(48);memcpy(root_.data(),"RTENINIT",8);put32(root_.data()+8,6);put32(root_.data()+44,F::FreshNativeFacts::crc(root_.data(),44));F::nvs["rnr00000006"]["root"]=root_;}
  if(mode==5)F::nvs["rnr00000006"]["root"]={5};
  if(mode>=6){std::filesystem::create_directories(F::rootPath+"/n00000006");std::ofstream(F::rootPath+"/n00000006/"+(mode==6?"replica-a.bin":mode==7?".pending":"foreign"))<<"valuable";}
  const auto before=F::nvs;const auto original=sd;NativeSd s(sd);const int rc=s.execute();if(rc!=C::Conflict)fprintf(stderr,"mode %u rc %d\n",mode,rc);assert(rc==C::Conflict&&F::nvs==before&&sd==original&&!s.f.core.ready());F::onWorker=true;++count;
 }
 printf("Checked SD restore: %u native legacy/root/unknown/artifact conflicts preserve both sources PASS\n",count);
}
static void interrupted(){
 unsigned count=0;for(const char*kind:{"nvs-set","nvs-commit","write","partial","sync","rename-before","rename-after","read","close"})for(bool after:{false,true})for(unsigned cut=1;cut<100;++cut){
  reset();Bytes map;C::Identity id;auto sd=checkpoint(&map,&id);const auto original=sd;bool reached=false;
  {NativeSd s(sd);Reader::Candidate c;s.candidate(&c);assert(!s.restore.begin(s.attempt,c));F::armed=true;F::fault=kind;F::cut=cut;F::after=after;F::nativeCalls=F::fsCalls=0;F::onWorker=true;assert(s.restore.workerStep());F::armed=false;F::onWorker=false;reached=F::nativeCalls>=cut||F::fsCalls>=cut;Completion::Result result{};int rc=s.restore.collect(&result);if(rc==C::Ok){assert(!s.attempt.release());}else assert(rc==C::Retained);F::onWorker=true;}
  assert(F::handles.empty()&&iterators.empty()&&sd==original);F::reset();
  {F::Fixture f;int rc=f.core.load();f.store.finishOperation();if(rc==C::Ok)exactMap(f.core,map,id);else assert(rc==C::Missing||rc==C::Initializing);}
  {NativeSd retry(sd);F::onWorker=true;int rc=retry.f.core.load();retry.f.store.finishOperation();F::onWorker=false;if(rc!=C::Ok){assert(retry.execute()==C::Ok);}exactMap(retry.f.core,map,id);assert(sd==original);F::onWorker=true;}
  if(!reached)break;
  ++count;assert(cut<99);
 }
 printf("Checked SD restore: %u reached native write/read/commit/partial/close errors recover exact image after reset PASS\n",count);
}
static void sdConflicts(){
 for(unsigned mode=0;mode<12;++mode){if(mode==9)continue;reset();auto card=checkpoint();if(mode==0)addLegacy(card);if(mode==1)addLegacy(card,true);if(mode==2)card["/RTECFG/00000006/settings.index"]={1,2,3};if(mode==3)card["/RTECFG/00000006/unknown"]={1};if(mode==4)card["/RTECFG/00000006/nr-old-generation"]={2};if(mode==5)card["/RTECFG/00000006/.replica.next"]={3};if(mode==6)card["/RTECFG/00000006/replica.index.risc-replace"]={4};if(mode==8)for(unsigned i=0;i<130;++i)card["/RTECFG/00000006/unknown-"+std::to_string(i)]={5};if(mode==9){card["/RTECFG/00000006/replica.index.risc-replace"]=card.at(Index);card.erase(Index);}if(mode==10){addLegacy(card);card["/RTECFG/00000006/settings.index.risc-replace"]=card.at("/RTECFG/00000006/settings.index");card.erase("/RTECFG/00000006/settings.index");}if(mode==11){addLegacy(card,true);card["/RTECFG/00000006/settings.kv.risc-replace"]=card.at("/RTECFG/00000006/settings.kv");card.erase("/RTECFG/00000006/settings.kv");}NativeSd s(card);if(mode==7)s.sd.dirs.insert("/RTECFG/00000006/foreign");const auto original=card;const auto native=F::nvs;s.catalog();Reader::Candidate c;assert(s.reader.takeRestoreCandidate(&c)==C::Conflict&&!c.valid()&&s.reader.exitSafe()&&card==original&&F::nvs==native);assert(!s.attempt.release());F::onWorker=true;}
 puts("Checked SD restore: 11 mixed legacy/corrupt/unknown/orphan/stage/legacy-backup/directory/bounded catalog conflicts refuse source selection PASS");
}
static void laterCheckpointRestore(){
 reset();auto card=checkpoint();const auto first=card;Bytes selected;
 {NativeSd s(card);assert(s.execute()==0);F::onWorker=true;assert(F::put(s.f.core,"later","explicit-user-edit")==0);s.f.store.finishOperation();DirectorySd output(card);Hash hash;T checkpoint_(hash.api(),output.guard());assert(checkpoint_.start(s.f.core,output.volume())==0);F::onWorker=false;output.run(checkpoint_);assert(checkpoint_.state()==T::State::Verified);F::onWorker=true;assert(checkpoint_.acknowledgeNative()==0&&s.f.core.identity().generation==20);C::Snapshot snapshot;assert(s.f.core.captureSnapshot(&snapshot)==0);selected=Bytes(snapshot.map(),snapshot.map()+snapshot.mapSize());s.f.store.finishOperation();}
 unsigned oldPages=0;for(const auto&item:first)if(item.first!=Index){assert(card.at(item.first)==item.second);++oldPages;}assert(oldPages==2&&card.size()==5);
 reset();const auto second=card;{NativeSd erased(card);assert(erased.execute()==C::Ok&&erased.f.core.identity().generation==20&&card==second);C::Snapshot snapshot;assert(erased.f.core.captureSnapshot(&snapshot)==0&&snapshot.mapSize()==selected.size()&&!memcmp(snapshot.map(),selected.data(),selected.size()));F::onWorker=true;}
 puts("Normal checkpoint restore: actual writer generation 19 -> 20, native erase, exact generation-20 restore; both old immutable pages remain byte-for-byte PASS");
}
static void historicalPageGrammar(){
 for(unsigned mode=0;mode<13;++mode){reset();Bytes map;C::Identity id;auto card=checkpoint(&map,&id);auto old=id;old.generation=18;std::string name=pagePath(old,0);Bytes bytes(4096,0xb7);
  if(mode==1){old.generation=19;old.digest[0]^=1;name=pagePath(old,0);}
  if(mode==2){old.generation=20;name=pagePath(old,0);}
  if(mode==3)name.back()='A';
  if(mode==4)name.erase(name.size()-1);
  if(mode==5)name+='0';
  if(mode==6)name[name.size()-10]='x';
  if(mode==7)bytes.clear();
  if(mode==8)bytes.resize(4097);
  if(mode==9)name=pagePath(old,16);
  if(mode==10){name=pagePath(old,15);bytes.resize(4073);}
  if(mode==11){name=pagePath(old,15);bytes.resize(4072);}
  if(mode==12){old.generation=0;name=pagePath(old,0);bytes.resize(1);}
  card[name]=bytes;const auto before=card;NativeSd s(card);if(mode==0||mode==1||mode==2||mode==7||mode==11||mode==12){assert(s.execute()==C::Ok);exactMap(s.f.core,map,id);}else{s.catalog();Reader::Candidate candidate;assert(s.reader.takeRestoreCandidate(&candidate)==C::Conflict&&!candidate.valid()&&F::nvs.empty());assert(!s.attempt.release());}assert(card==before);F::onWorker=true;
 }
 puts("Historical pages: canonical unselected pages at all generations and empty partial pages preserved; malformed names and impossible page sizes remain Conflict PASS");
}
// Faithful RTENSD1 writer output: each generation has its own immutable page
// names and complete canonical map, with only the last committed index retained.
static Files manyGenerationCard(unsigned generations,Bytes*out=nullptr,C::Identity*selected=nullptr){
 Bytes map;checkpoint(&map);Files card;C::Identity id{};
 for(unsigned generation=0;generation<generations;++generation){map.back()=uint8_t(generation);id=identity(map,31,generation);save(card,map,31,id);}
 if(out)*out=map;
 if(selected)*selected=id;
 return card;
}
static void manyGenerationRestore(){
 size_t peak=0;for(unsigned generations:{1u,65u,257u}){reset();Bytes map;C::Identity id;auto card=manyGenerationCard(generations,&map,&id);const auto original=card;CatalogAlloc memory;
  {NativeSd s(card,Owner,memory.api());s.sd.reverse=true;s.sd.dots=true;assert(s.execute()==C::Ok);exactMap(s.f.core,map,id);const unsigned sweeps=std::max(1u,(2*(generations-1)+K::HistoryWindow-1)/K::HistoryWindow);assert(card==original&&s.sd.opens==2*sweeps);F::onWorker=true;}
  assert(!memory.live);if(!peak)peak=memory.peak;else assert(memory.peak==peak);
 }
 printf("Many-generation restore: 1/65/257 generations, up to 515 entries, shuffled scans and dots, exact current opaque map, all old bytes untouched; constant allocator peak %zu bytes PASS\n",peak);
}
static void historyWindowBoundaries(){
 for(unsigned history:{63u,64u,65u}){reset();Bytes map;C::Identity id;auto card=manyGenerationCard(35,&map,&id);for(auto it=card.begin();card.size()>history+3;){if(it->first!=Index&&it->first.find("nr-0000000000000022-")==std::string::npos)it=card.erase(it);else ++it;}assert(card.size()==history+3);const auto original=card;NativeSd s(card);s.sd.reverse=true;assert(s.execute()==C::Ok&&card==original&&s.sd.opens==2*((history+K::HistoryWindow-1)/K::HistoryWindow));exactMap(s.f.core,map,id);F::onWorker=true;}
 puts("History window boundaries: exactly 63/64/65 historical descriptors restore without dropped or repeated entries PASS");
}
static uint32_t(*historyOpenOriginal)(void*,const char*)=nullptr;
static unsigned historyFaultMode=0;
static uint32_t historyOpenFault(void*p,const char*path){
 const uint32_t handle=historyOpenOriginal(p,path);auto&s=DirectorySd::self(p);if(!handle||s.directory!="/RTECFG/00000006")return handle;
 auto&entries=s.dirEntries;std::vector<size_t> history;for(size_t i=0;i<entries.size();++i)if(!strncmp(entries[i].name,"nr-",3)&&strncmp(entries[i].name,"nr-0000000000000080-",20))history.push_back(i);assert(history.size()==256);
 if(historyFaultMode<3){const size_t i=history[historyFaultMode==0?0:historyFaultMode==1?history.size()-1:K::HistoryWindow-1];entries.push_back(entries[i]);}
 if(historyFaultMode==3){auto alias=entries[history.back()];alias.name[0]='N';entries.push_back(alias);}
 if(historyFaultMode==11&&s.opens>=5)--entries[history.front()].size;
 if(s.opens==2){
  if(historyFaultMode==4)--entries[history.front()].size;
  if(historyFaultMode==5)entries.erase(entries.begin()+history.front());
  if(historyFaultMode==6){risc_storage_dirent_v1 unknown{};strcpy(unknown.name,"unexplained");unknown.size=1;entries.push_back(unknown);}
  if(historyFaultMode==7){auto it=std::find_if(entries.begin(),entries.end(),[](const auto&e){return !strncmp(e.name,"nr-0000000000000080-",20);});assert(it!=entries.end());entries.erase(it);}
  if(historyFaultMode==8){auto it=std::find_if(entries.begin(),entries.end(),[](const auto&e){return !strcmp(e.name,"replica.index");});assert(it!=entries.end());entries.push_back(*it);}
  if(historyFaultMode==9){auto&e=entries[history.front()];e.name[20]=e.name[20]=='0'?'1':'0';}
  if(historyFaultMode==10){risc_storage_dirent_v1 legacy{};strcpy(legacy.name,"settings.kv");legacy.size=1;entries.push_back(legacy);}
 }
 return handle;
}
static void manyGenerationConflicts(){
 for(historyFaultMode=0;historyFaultMode<12;++historyFaultMode){reset();auto card=manyGenerationCard(129);const auto original=card;NativeSd s(card);auto&api=s.sd.download.base.base.fs.state.prepared.base.sleep.terminal.power.volume.base;historyOpenOriginal=api.dir_open;api.dir_open=historyOpenFault;
  s.catalog();Reader::Candidate candidate;assert(s.reader.takeRestoreCandidate(&candidate)==C::Conflict&&!candidate.valid()&&s.reader.exitSafe()&&F::nvs.empty()&&card==original);assert(!s.attempt.release());F::onWorker=true;
 }
 for(unsigned mode=0;mode<4;++mode){reset();auto card=manyGenerationCard(129);if(mode==0)addLegacy(card);if(mode==1)card["/RTECFG/00000006/nr-malformed"]={1};if(mode==2)card["/RTECFG/00000006/replica.index.risc-replace"]=card.at(Index);if(mode==3)card["/RTECFG/00000006/.replica.next"]={1};const auto original=card;NativeSd s(card);s.catalog();Reader::Candidate c;assert(s.reader.takeRestoreCandidate(&c)==C::Conflict&&F::nvs.empty()&&card==original);assert(!s.attempt.release());F::onWorker=true;}
 // A work cutoff is never exhaustion and must not mint a partial-inventory permit.
 for(uint32_t work:{1u,260u,1000u}){reset();auto card=manyGenerationCard(129);const auto original=card;NativeSd s(card);K::Limits limits;limits.restoreWork=work;s.catalog(limits);Reader::Candidate c;assert(s.reader.takeRestoreCandidate(&c)==C::Conflict&&!c.valid()&&F::nvs.empty()&&card==original);assert(!s.attempt.release());F::onWorker=true;}
 puts("Many-generation catalog: early/late/boundary duplicates, case aliases, sweep descriptor/name changes, omitted/duplicate selected files, late unknown/legacy sources, backups/staging and work cutoff all refuse without writes PASS");
}
static void manyGenerationCustody(){
 for(unsigned mode=0;mode<4;++mode){reset();auto card=manyGenerationCard(129);const auto original=card;NativeSd s(card);s.acquire();assert(!s.reader.prepareRestoreNative(s.attempt));F::onWorker=true;assert(s.reader.preparationWorkerStep());F::onWorker=false;assert(!s.reader.collectPreparation());
  while(s.sd.opens<2){assert(s.reader.state()==K::State::Running);s.reader.step();s.sdStep();}
  if(mode==0)s.reader.cancel();
  if(mode==1)s.sd.retained=true;
  if(mode==2)++s.epoch;
  if(mode==3)F::lease=false;
  while(s.reader.state()==K::State::Running){s.reader.step();s.sdStep();}
  assert(F::nvs.empty()&&card==original);Reader::Candidate c;assert(s.reader.takeRestoreCandidate(&c)!=C::Ok&&!c.valid());
  if(mode==0){assert(s.reader.state()==K::State::Cancelled&&s.reader.exitSafe());assert(!s.attempt.release());}else if(mode==1)assert(s.reader.state()==K::State::Retained&&!s.reader.exitSafe());else assert(s.reader.state()==K::State::Failed||s.reader.state()==K::State::Retained);
  F::lease=true;F::onWorker=true;
 }
 puts("Many-generation catalog: between-window cancellation, Retained, epoch and lease loss preserve all evidence and mint no candidate PASS");
}
static void manyGenerationSourceRecheck(){
 for(unsigned mode=0;mode<5;++mode){reset();auto card=manyGenerationCard(129);NativeSd s(card);s.acquire();assert(!s.reader.prepareRestoreNative(s.attempt));F::onWorker=true;assert(s.reader.preparationWorkerStep());F::onWorker=false;assert(!s.reader.collectPreparation());
  // The preflight reader has closed and the first inventory is being scanned.
  while(!s.sd.opens){assert(s.reader.state()==K::State::Running);s.reader.step();s.sdStep();}
  if(mode==0){auto&index=card.at(Index);index[32]^=1;put32(index.data()+80,catalogCrc(index.data(),80));}
  if(mode==1){auto&index=card.at(Index);index[24]^=1;put32(index.data()+80,catalogCrc(index.data(),80));}
  if(mode==2)s.readHash.broken=true;
  if(mode>=3){while(!s.sd.dirCloses){assert(s.reader.state()==K::State::Running);s.reader.step();s.sdStep();}if(mode==3)s.readHash.broken=true;else s.sd.retained=true;}
  const auto changed=card;while(s.reader.state()==K::State::Running){s.reader.step();s.sdStep();}
  Reader::Candidate c;assert(s.reader.takeRestoreCandidate(&c)!=C::Ok&&!c.valid()&&F::nvs.empty()&&card==changed);if(mode==4)assert(s.reader.state()==K::State::Retained&&!s.reader.exitSafe());else{assert(s.reader.exitSafe());assert(!s.attempt.release());}F::onWorker=true;
 }
 for(uint32_t work:{0u,K::RestoreWorkMax+1}){reset();auto card=manyGenerationCard(2);NativeSd s(card);s.acquire();K::Limits limits;limits.restoreWork=work;assert(!s.reader.prepareRestoreNative(s.attempt,limits));F::onWorker=true;assert(s.reader.preparationWorkerStep());F::onWorker=false;assert(s.reader.collectPreparation()==C::Invalid&&s.reader.exitSafe());assert(!s.attempt.release());F::onWorker=true;}
 puts("Many-generation catalog: changed checked lineage/generation, open/closed hash failure, closed-window Retained and invalid resource bounds cannot mint a candidate PASS");
}
static decltype(F::nvs) restoreReceipt(Files&sd){
 reset();{NativeSd s(sd);Reader::Candidate c;s.candidate(&c);assert(!s.restore.begin(s.attempt,c));F::fault="nvs-set";F::cut=1;F::after=F::armed=true;F::onWorker=true;assert(s.restore.workerStep());F::armed=false;F::onWorker=false;Completion::Result result{};assert(s.restore.collect(&result)==C::Retained&&!s.restore.exitSafe()&&!s.attempt.exitSafe());F::onWorker=true;}
 assert(root().size()==48&&!memcmp(root().data(),"RTENSI1",8));F::reset();return F::nvs;
}
static void foreignSources(){
 auto sd=checkpoint();const auto receipt=restoreReceipt(sd);unsigned count=0;
 for(unsigned mode=0;mode<12;++mode){reset();F::nvs=receipt;Bytes map;C::Identity id;auto card=checkpoint(&map,&id);uint8_t owner[16];memcpy(owner,Owner,16);
  if(mode==0){id.lineage[1]^=1;save(card,map,31,id);}
  if(mode==1){++id.generation;card.clear();save(card,map,31,id);}
  if(mode==2){map[20]^=1;id=identity(map,31,19);card.clear();save(card,map,31,id);}
  if(mode==3)owner[3]^=1;
  if(mode==4){auto&r=F::nvs["rnr00000006"]["root"];r[12]^=1;put32(r.data()+44,F::FreshNativeFacts::crc(r.data(),44));}
  if(mode==5)F::nvs["rte00000006"]["late"]={1};
  if(mode==6)F::nvs["kvj00000006"]["state2"]={2};
  if(mode==7)F::nvs["kvd00000006"]["late"]={3};
  if(mode==8)F::nvs["rnr00000006"]["unknown"]={4};
  if(mode==9)F::nvs["rnr00000006"]["next"]={5};
  if(mode>=10){std::filesystem::create_directories(F::rootPath+"/n00000006");std::ofstream(F::rootPath+"/n00000006/"+(mode==10?".pending":"replica-b.bin"))<<"foreign";}
  const auto before=F::nvs;const auto beforeCard=card;NativeSd s(card,owner);assert(s.execute()==C::Conflict&&F::nvs==before&&card==beforeCard&&!s.f.core.ready());F::onWorker=true;++count;
 }
 printf("Checked SD restore: %u foreign lineage/generation/map/owner/receipt/native-source/artifact resumes refuse without writes PASS\n",count);
}
static void edgeMapsAndPrefixes(){
 for(uint32_t size:{0u,S::ImageMax-C::Header-4,S::ImageMax-C::Header-3}){reset();Bytes m;uint32_t count=0;while(m.size()<size){const auto at=m.size();const uint32_t n=std::min<uint32_t>(C::ValueMax,size-at-20);m.resize(at+20+n);char name[16];snprintf(name,sizeof name,"opaque%05u",count++);strcpy(reinterpret_cast<char*>(m.data()+at),name);put32(m.data()+at+16,n);memset(m.data()+at+20,int(count),n);}auto id=identity(m,count,0);Files card;save(card,m,count,id);NativeSd s(card);if(size>S::ImageMax-C::Header-4){s.catalog();Reader::Candidate c;assert(s.reader.takeRestoreCandidate(&c)==C::Conflict&&!c.valid());assert(!s.attempt.release());}else {assert(s.execute()==0);C::Snapshot snap;assert(s.f.core.captureSnapshot(&snap)==0&&snap.mapSize()==size&&snap.count()==count&&s.f.core.identity().generation==0&&(!size||!memcmp(snap.map(),m.data(),size)));}F::onWorker=true;}
 auto m=mapRows({"one"});auto id=identity(m,1,7);Files card;save(card,m,1,id);const auto receipt=restoreReceipt(card);Bytes object;
 {NativeSd s(card);assert(s.execute()==0);F::onWorker=true;std::ifstream input(F::rootPath+"/n00000006/replica-a.bin",std::ios::binary);object=Bytes(std::istreambuf_iterator<char>(input),{});assert(object.size()==24+C::Header+m.size()+4);}
 for(size_t n=0;n<=object.size();++n){reset();F::nvs=receipt;Bytes seq(24);memcpy(seq.data(),"RTENSEQ",8);put32(seq.data()+8,8);put32(seq.data()+20,F::FreshNativeFacts::crc(seq.data(),20));F::nvs["rnr00000006"]["next"]=seq;auto partial=object;put32(partial.data()+8,16);std::filesystem::create_directories(F::rootPath+"/n00000006");{std::ofstream out(F::rootPath+"/n00000006/.pending",std::ios::binary);out.write(reinterpret_cast<const char*>(partial.data()),n);}NativeSd s(card);assert(s.execute()==0&&s.f.store.activeObject()==18);C::Snapshot snap;assert(s.f.core.captureSnapshot(&snap)==0&&snap.mapSize()==m.size()&&!memcmp(snap.map(),m.data(),m.size()));F::onWorker=true;}
 printf("Checked SD restore: empty/maximum/over-capacity maps, zero generation, and %zu exact interrupted object prefixes PASS\n",object.size()+1);
}
static unsigned callbackCount=0,callbackMode=0;
static void changeDuringNative(const char*point){if(strcmp(point,"nvs-set-after"))return;if(++callbackCount!=(callbackMode==0?1u:2u))return;if(callbackMode==0)F::nvs["rte00000006"]["late"]={9};else if(callbackMode==1){auto&r=F::nvs["rnr00000006"]["root"];r[12]^=1;put32(r.data()+44,F::FreshNativeFacts::crc(r.data(),44));}else F::nvs["rnr00000006"]["unknown"]={8};}
static void changedDuringWrite(){
 for(callbackMode=0;callbackMode<3;++callbackMode){reset();callbackCount=0;auto card=checkpoint();NativeSd s(card);F::powerCheckpoint=changeDuringNative;const int rc=s.execute();F::powerCheckpoint=nullptr;assert(rc!=0&&!s.f.core.ready()&&memcmp(root().data(),"RTENROOT",8));assert(callbackCount>0);F::onWorker=true;}
 for(const char*kind:{"RTENFI1","RTENLI1"}){reset();auto card=checkpoint();Bytes r(48);memcpy(r.data(),kind,8);put32(r.data()+8,6);r[12]=1;put32(r.data()+44,F::FreshNativeFacts::crc(r.data(),44));F::nvs["rnr00000006"]["root"]=r;const auto before=F::nvs;NativeSd s(card);assert(s.execute()==C::Conflict&&F::nvs==before);F::onWorker=true;}
 puts("Checked SD restore: mid-write competing source/receipt/metadata changes and foreign typed receipt refusal PASS");
}
static void custodyAndCancellation(){
 reset();auto sd=checkpoint();{NativeSd s(sd);const auto before=F::nvs;assert(s.execute(true)==C::Stale&&F::nvs==before&&!s.f.core.ready());F::onWorker=true;}
 for(unsigned mode=0;mode<5;++mode){reset();auto card=checkpoint();NativeSd s(card);Reader::Candidate c;s.candidate(&c);const auto nvs=F::nvsTotal,fs=F::fsTotal;
  if(mode==0){Attempt other({6,55,7,71,s.sd.volume()},s.attemptHooks());assert(s.restore.begin(other,c)==C::Conflict&&c.valid());assert(!s.attempt.release());}
  if(mode==1){assert(!s.attempt.openChild());int rc=s.attempt.close();while(rc==RISC_STORAGE_ASYNC_PENDING){s.sdStep();rc=s.attempt.close();}assert(!rc);assert(s.restore.begin(s.attempt,c)==C::Conflict&&c.valid());assert(!s.attempt.release());}
  if(mode>=2){assert(!s.restore.begin(s.attempt,c));if(mode==2||mode==3)F::lease=false;if(mode==3)s.restore.cancel();if(mode==4)++s.epoch;F::onWorker=true;assert(s.restore.workerStep());F::onWorker=false;Completion::Result result{};assert(s.restore.collect(&result)==C::Retained&&!s.restore.exitSafe()&&!s.attempt.exitSafe());assert(F::nvsTotal==nvs&&F::fsTotal==fs);F::lease=true;}
  F::onWorker=true;
 }
 // An ordinary/raw checked reader proves historical bytes but cannot mint native
 // source/session authority for restore.
 reset();{auto card=checkpoint();NativeSd s(card);Reader::Candidate c;Sd plain(card);Hash h;Reader detached(s.f.core,h.api(),plain.guard());assert(detached.start(plain.volume())==0);plain.run(detached);assert(detached.take(&c)==0);s.acquire();assert(s.restore.begin(s.attempt,c)==C::Conflict&&c.valid());assert(!s.attempt.release());F::onWorker=true;}
 // Late provider Retained never invokes the observer on the native worker; a
 // native commit can win, but collection cannot release its custody.
 reset();{auto card=checkpoint();NativeSd s(card);Reader::Candidate c;s.candidate(&c);assert(!s.restore.begin(s.attempt,c));F::onWorker=true;assert(s.restore.workerStep());F::onWorker=false;s.restore.cancel();s.sd.retained=true;Completion::Result result{};assert(s.restore.collect(&result)==C::Retained&&!s.restore.exitSafe()&&!s.attempt.exitSafe()&&s.f.core.ready());F::onWorker=true;}
 // Issue-time retention must be visible through the controller immediately.
 reset();{auto card=checkpoint();NativeSd s(card);Reader::Candidate c;s.candidate(&c);s.sd.retained=true;assert(s.restore.begin(s.attempt,c)==C::Retained&&s.restore.retained()&&!s.restore.exitSafe());F::onWorker=true;}
 puts("Checked SD restore: queued cancellation, task/lease/epoch loss, foreign or stale sessions, detached candidate and late/issue Retained custody PASS");
}
int main(int argc,char**argv){assert(argc==2);F::rootPath=argv[1];setbuf(stdout,nullptr);roundtrip();conflicts();sdConflicts();laterCheckpointRestore();historicalPageGrammar();manyGenerationRestore();historyWindowBoundaries();manyGenerationConflicts();manyGenerationCustody();manyGenerationSourceRecheck();interrupted();foreignSources();edgeMapsAndPrefixes();changedDuringWrite();custodyAndCancellation();reset();puts("Checked SD restore host suite PASS");}
