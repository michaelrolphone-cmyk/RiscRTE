// Actual checkpoint state machine output, checked native erase/restore and
// copied catalog custody. The media fault model is separate from X4/FatFs tests.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main previous_restore_main
#include "native_checked_sd_restore_test.cpp"
#undef main
#pragma GCC diagnostic pop
static void checkedMap(C&core,const Files&card){
 const auto&index=card.count(Index)?card.at(Index):card.at(std::string(Index)+".risc-replace");
 C::Identity id{};id.known=true;id.generation=uint64_t(index[24]);memcpy(id.lineage,index.data()+32,16);memcpy(id.digest,index.data()+48,32);
 Bytes expected;for(unsigned page=0;expected.size()<uint32_t(index[12]|index[13]<<8|index[14]<<16|index[15]<<24);++page){const auto&b=card.at(pagePath(id,page));expected.insert(expected.end(),b.begin(),b.end());}
 C::Snapshot snapshot;assert(!core.captureSnapshot(&snapshot));assert(snapshot.mapSize()==expected.size()&&(!expected.size()||!memcmp(snapshot.map(),expected.data(),expected.size()))&&snapshot.base().generation==id.generation&&!memcmp(snapshot.base().digest,id.digest,32));
}
static void writerCuts(){
 Native initial;Files original;seed(initial,original);unsigned calls=0;
 {auto n=initial;auto card=original;C core(6,n.api(),limits(),C::heap());assert(!core.load());Sd sd(card);Hash hash;T writer(hash.api(),sd.guard());assert(!writer.start(core,sd.volume()));sd.run(writer);assert(writer.state()==T::State::Verified);calls=sd.io;writer.cancel();}
 unsigned cases=0,backupOnly=0,staged=0,publishedBackup=0,emptyPages=0;
 for(bool after:{false,true})for(unsigned cut=1;cut<=calls;++cut){
  reset();auto n=initial;auto card=original;
  {C core(6,n.api(),limits(),C::heap());assert(!core.load());Sd sd(card);Hash hash;T writer(hash.api(),sd.guard());assert(!writer.start(core,sd.volume()));sd.crash=true;sd.after=after;sd.fault=cut;try{sd.run(writer);}catch(const Sd::Cut&){}assert(sd.hit);}
  const auto interrupted=card;backupOnly+=!card.count(Index);staged+=card.count("/RTECFG/00000006/.replica.next");publishedBackup+=card.count(Index)&&card.count(std::string(Index)+".risc-replace");for(const auto&f:card)if(f.first.find("/nr-")!=std::string::npos&&f.second.empty())++emptyPages;
  {NativeSd s(card);const int rc=s.execute();if(rc)fprintf(stderr,"cut %u after %u restore %d\n",cut,after,rc);assert(!rc&&card==interrupted);checkedMap(s.f.core,card);assert(!s.sd.mutations);F::onWorker=true;}
  F::reset();{F::Fixture restarted;assert(!restarted.core.load());checkedMap(restarted.core,card);}++cases;
 }
 assert(backupOnly&&staged&&publishedBackup&&emptyPages);
 printf("Interrupted writer: %u before/after cuts; backup-only=%u staged=%u published-with-backup=%u empty-pages=%u; erase/restore and restart exact, every SD byte preserved PASS\n",cases,backupOnly,staged,publishedBackup,emptyPages);
}
static void relatedArtifacts(){
 for(unsigned mode=0;mode<8;++mode){reset();Bytes map;C::Identity id;auto card=checkpoint(&map,&id);auto next=id;++next.generation;Bytes stage(T::IndexBytes);T::makeIndex(stage.data(),6,map.size(),31,next);
  if(mode==0)card["/RTECFG/00000006/.replica.next"]=stage;
  if(mode==1){stage.resize(0);card["/RTECFG/00000006/.replica.next"]=stage;}
  if(mode==2){stage.resize(42);card["/RTECFG/00000006/.replica.next"]=stage;}
  if(mode==3){auto prior=id;--prior.generation;Bytes b(T::IndexBytes);T::makeIndex(b.data(),6,map.size(),31,prior);card[std::string(Index)+".risc-replace"]=b;}
  if(mode==4){card[std::string(Index)+".risc-replace"]=card.at(Index);card.erase(Index);card["/RTECFG/00000006/.replica.next"]=stage;}
  if(mode>=5){auto orphan=id;orphan.generation=mode==5?id.generation:mode==6?UINT64_MAX:0;orphan.digest[0]^=1;card[pagePath(orphan,0)]=Bytes(mode==7?0:19,0xa5);}
  const auto before=card;NativeSd s(card);assert(!s.execute()&&card==before&&!s.sd.mutations);exactMap(s.f.core,map,id);F::onWorker=true;
 }
 puts("Related interrupted artifacts: complete/partial/empty stage, prior backup, backup-only rollback source, same/newer/zero orphan generations restore without SD mutation PASS");
}
static void hostileArtifacts(){
 for(unsigned mode=0;mode<18;++mode){reset();Bytes map;C::Identity id;auto card=checkpoint(&map,&id);auto next=id;++next.generation;Bytes stage(T::IndexBytes);T::makeIndex(stage.data(),6,map.size(),31,next);
  if(mode<8){if(mode==0)stage[0]^=1;if(mode==1)stage[8]^=1;if(mode==2)stage[24]+=2;if(mode==3)stage[32]^=1;if(mode==4)stage[80]^=1;if(mode==5)stage.push_back(0);if(mode==6){put32(stage.data()+12,65513);put32(stage.data()+80,catalogCrc(stage.data(),80));}if(mode==7){put32(stage.data()+16,65535);put32(stage.data()+80,catalogCrc(stage.data(),80));}card["/RTECFG/00000006/.replica.next"]=stage;}
  if(mode==8)card[std::string(Index)+".risc-replace"]=stage;
  if(mode==9){auto prior=id;--prior.generation;prior.lineage[0]^=1;T::makeIndex(stage.data(),6,map.size(),31,prior);card[std::string(Index)+".risc-replace"]=stage;}
  if(mode==10){card[std::string(Index)+".risc-replace"]=card.at(Index);card.at(Index)[0]^=1;}
  if(mode==11){card[std::string(Index)+".risc-replace"]=card.at(Index);card.erase(Index);addLegacy(card);}
  if(mode==12)addLegacy(card,true);
  if(mode==13)card["/RTECFG/00000006/.other.next"]={1};
  if(mode==14)card[pagePath(id,15)]={1};
  if(mode==15){auto foreign=next;foreign.digest[0]^=1;card[pagePath(foreign,0)]=Bytes(4097);}
  if(mode==16){auto name=pagePath(next,0);name[17]='N';card[name]={1};}
  if(mode==17){card[std::string(Index)+".risc-replace"]={1};card.erase(Index);}
  const auto before=card;NativeSd s(card);s.catalog();Reader::Candidate candidate;assert(s.reader.takeRestoreCandidate(&candidate)==C::Conflict&&!candidate.valid()&&card==before&&F::nvs.empty()&&!s.sd.mutations);assert(!s.attempt.release());F::onWorker=true;
 }
 puts("Hostile artifacts: foreign/malformed stage and backup, corrupt primary never fallback, legacy mixture, unknown names and impossible/case-aliased pages remain closed with bytes preserved PASS");
}
static void syncGate(){
 for(unsigned mode=0;mode<6;++mode){reset();auto card=checkpoint();NativeSd s(card);F::onWorker=true;Bytes map;C::Identity id;checkpoint(&map,&id);std::vector<C::Value>rows;for(size_t at=0;at<map.size();){uint32_t n=uint32_t(map[at+16])|(uint32_t(map[at+17])<<8);rows.push_back({reinterpret_cast<const char*>(map.data()+at),map.data()+at+20,n});at+=20+n;}assert(!s.f.core.initialize(rows.data(),rows.size(),id));assert(!F::put(s.f.core,"later","value"));
  const uint8_t incarnation[16]={1};Hash h;Sd output(card);T writer(h.api(),output.guard());assert(!writer.startDurable(s.f.core,output.volume(),incarnation));C::ExportMarker marker;assert(!s.f.core.preparedExport(&marker));writer.cancel();output.run(writer);s.f.store.finishOperation();
  Bytes stage(T::IndexBytes);T::makeIndex(stage.data(),6,marker.mapSize(),marker.count(),marker.target());if(mode==1)stage.resize(40);if(mode==2)stage[49]^=1;if(mode==3)stage.resize(0);if(mode==4)stage[24]^=1;if(mode==5)assert(!s.f.core.retireExport(marker,marker.base()));card["/RTECFG/00000006/.replica.next"]=stage;
  F::onWorker=false;s.acquire();const auto before=card;assert(!s.reader.prepareSynchronizationNative(s.attempt));F::onWorker=true;assert(s.reader.preparationWorkerStep());F::onWorker=false;assert(!s.reader.collectPreparation());while(s.reader.state()==K::State::Running){s.reader.step();s.sdStep();}Reader::Candidate c;const int rc=s.reader.takeRestoreCandidate(&c);assert(rc==((mode==2||mode==4)?C::Conflict:C::Ok)&&card==before&&!s.sd.mutations);assert(!s.attempt.release());F::onWorker=true;
 }
 puts("Sync inventory: stage descriptors remain read-only after marker retirement; malformed CRC refuses and no cleanup authority is minted PASS");
}
static void targetBoundStages(){
 using Stage=RiscExperimental::SdReplicaStage;
 for(unsigned mode=0;mode<17;++mode){reset();Bytes map;C::Identity id;auto card=checkpoint(&map,&id);auto staged=id;++staged.generation;staged.digest[0]^=1;char path[Stage::PathBytes];assert(Stage::makePath(path,sizeof path,6,staged)&&strlen(path)==141&&strlen(path+17)==124);C::Identity parsed;assert(Stage::parseName(path+17,&parsed)&&parsed.generation==staged.generation&&!memcmp(parsed.lineage,staged.lineage,16)&&!memcmp(parsed.digest,staged.digest,32));
  Bytes descriptor(T::IndexBytes);T::makeIndex(descriptor.data(),6,map.size(),31,staged);bool accepted=mode<7;
  if(mode==1)descriptor.resize(0);
  if(mode==2)descriptor.resize(50);
  if(mode==3){staged.generation=id.generation;assert(Stage::makePath(path,sizeof path,6,staged));T::makeIndex(descriptor.data(),6,map.size(),31,staged);}
  if(mode==4){staged.generation=1;assert(Stage::makePath(path,sizeof path,6,staged));T::makeIndex(descriptor.data(),6,map.size(),31,staged);}
  if(mode==5){strcpy(path,"/RTECFG/00000006/.replica.next");--staged.generation;T::makeIndex(descriptor.data(),6,map.size(),31,staged);}
  if(mode==6){strcpy(path,"/RTECFG/00000006/.replica.next");staged.generation=1;T::makeIndex(descriptor.data(),6,map.size(),31,staged);}
  if(mode==7)path[17+5]='A';
  if(mode==8)path[17+119]='x';
  if(mode==9){descriptor[48]^=1;put32(descriptor.data()+80,catalogCrc(descriptor.data(),80));}
  if(mode==10){descriptor[24]^=1;put32(descriptor.data()+80,catalogCrc(descriptor.data(),80));}
  if(mode==11){staged.lineage[0]^=1;assert(Stage::makePath(path,sizeof path,6,staged));T::makeIndex(descriptor.data(),6,map.size(),31,staged);}
  if(mode==12)descriptor.push_back(0);
  if(mode==13)descriptor[80]^=1;
  if(mode==14){descriptor[8]^=1;put32(descriptor.data()+80,catalogCrc(descriptor.data(),80));}
  if(mode==15)path[strlen(path)-1]=0;
  if(mode==16){descriptor[32]^=1;put32(descriptor.data()+80,catalogCrc(descriptor.data(),80));}
  card[path]=descriptor;const auto before=card;NativeSd s(card);s.catalog();Reader::Candidate c;const int rc=s.reader.takeRestoreCandidate(&c);assert(rc==(accepted?C::Ok:C::Conflict)&&card==before&&!s.sd.mutations&&F::nvs.empty());if(accepted)assert(c.identity().generation==id.generation);assert(!s.attempt.release());F::onWorker=true;
 }
 // Multiple abandoned descriptors remain bounded ordinary inventory, not roots.
 reset();Bytes map;C::Identity id;auto card=checkpoint(&map,&id);for(unsigned i=1;i<=20;++i){auto staged=id;staged.generation+=i;staged.digest[0]=uint8_t(i);char path[Stage::PathBytes];assert(Stage::makePath(path,sizeof path,6,staged));Bytes b(T::IndexBytes);T::makeIndex(b.data(),6,map.size(),31,staged);card[path]=b;}const auto before=card;NativeSd s(card);assert(!s.execute()&&card==before&&!s.sd.mutations);F::onWorker=true;
 puts("Target-bound stage v2: maximum-length grammar, complete/empty/partial/current/older descriptors, 20 retained targets; foreign/name/content/CRC mismatches refuse without selection or mutation PASS");
}
static Files withStage(){Bytes map;C::Identity id;auto card=checkpoint(&map,&id);++id.generation;Bytes b(T::IndexBytes);T::makeIndex(b.data(),6,map.size(),31,id);card["/RTECFG/00000006/.replica.next"]=b;return card;}
static void artifactCustody(){
 unsigned cases=0;
 for(unsigned mode=0;mode<5;++mode)for(unsigned delay=0;delay<12;++delay){reset();auto card=withStage();const auto before=card;NativeSd s(card);s.acquire();assert(!s.reader.prepareRestoreNative(s.attempt));F::onWorker=true;assert(s.reader.preparationWorkerStep());F::onWorker=false;assert(!s.reader.collectPreparation());
  while(s.sd.dirCloses<2){s.reader.step();s.sdStep();assert(s.reader.state()==K::State::Running);}
  for(unsigned i=0;i<delay&&s.reader.state()==K::State::Running;++i){s.reader.step();s.sdStep();}
  if(mode==0)s.reader.cancel();
  if(mode==1)s.sd.retained=true;
  if(mode==2)++s.epoch;
  if(mode==3)F::lease=false;
  if(mode==4)s.sd.available=false;
  while(s.reader.state()==K::State::Running){s.reader.step();s.sdStep();}
  Reader::Candidate c;assert(s.reader.takeRestoreCandidate(&c)!=C::Ok&&!c.valid()&&F::nvs.empty()&&card==before&&!s.sd.mutations);
  if(mode==0){assert(s.reader.state()==K::State::Cancelled&&s.reader.exitSafe());assert(!s.attempt.release());}else if(mode==1)assert(s.reader.state()==K::State::Retained&&!s.reader.exitSafe());
  F::lease=true;F::onWorker=true;++cases;
 }
 printf("Artifact reads: %u queued/active/closed cancellation, epoch/lease/media loss and Retained boundaries mint no candidate and preserve all bytes PASS\n",cases);
}
static void synchronizationCapacity(){
 reset();Bytes map;unsigned count=0;while(map.size()<65360){const auto at=map.size();const unsigned n=std::min<size_t>(2000,65360-at-20);map.resize(at+20+n);snprintf(reinterpret_cast<char*>(map.data()+at),16,"key%04u",count++);put32(map.data()+at+16,n);memset(map.data()+at+20,'v',n);}Files card;auto id=identity(map,count,19);save(card,map,count,id);NativeSd s(card);F::onWorker=true;
 C::Value small{"local","x",1};assert(!s.f.core.initialize(&small,1,id));C::Snapshot snapshot;assert(!s.f.core.captureSnapshot(&snapshot));auto target=id;++target.generation;target.digest[0]^=1;C::ExportMarker marker;uint8_t incarnation[16]={1};assert(!s.f.core.prepareExport(snapshot,target,incarnation,&marker));s.f.store.finishOperation();F::onWorker=false;
 const auto before=card;s.acquire();assert(!s.reader.prepareSynchronizationNative(s.attempt));F::onWorker=true;assert(s.reader.preparationWorkerStep());F::onWorker=false;assert(!s.reader.collectPreparation());while(s.reader.state()==K::State::Running){s.reader.step();s.sdStep();}Reader::Candidate c;assert(!s.reader.takeRestoreCandidate(&c)&&c.mapSize()==65360&&card==before);assert(s.restore.begin(s.attempt,c)==C::Conflict&&c.valid());assert(!s.attempt.release());F::onWorker=true;
 puts("Sync catalog: prior large v1 base verified after smaller v2 marker capture; verification candidate cannot authorize native restore PASS");
}
static void historyCapacity(){
 size_t peak=0;
 for(unsigned generations:{5761u,5762u}){reset();Files card;auto map=mapRows({"one"});C::Identity selected{};for(unsigned g=0;g<generations;++g){map.back()=uint8_t(g);selected=identity(map,1,g);save(card,map,1,selected);}const auto before=card;CatalogAlloc memory;
  {NativeSd s(card,Owner,memory.api());s.catalog();Reader::Candidate candidate;const int rc=s.reader.takeRestoreCandidate(&candidate);const uint64_t sweeps=(generations-1+K::HistoryWindow-1)/K::HistoryWindow,required=2*uint64_t(generations+1)*sweeps;const auto next=std::count(s.sd.operations.begin(),s.sd.operations.end(),"dir-next");assert(rc==(generations==5761?C::Ok:C::Conflict)&&card==before&&F::nvs.empty()&&!s.sd.mutations&&s.sd.opens==2*sweeps);if(!rc)assert(candidate.count()==1&&candidate.mapSize()==map.size()&&candidate.identity().generation==generations-1&&!memcmp(candidate.map(),map.data(),map.size()));assert(!s.attempt.release());printf("History capacity: generations=%u required_entries=%llu sweeps=%u next_calls=%lld result=%s allocator_peak=%zu PASS\n",generations,(unsigned long long)required,s.sd.opens,(long long)next,rc?"Conflict":"verified",memory.peak);F::onWorker=true;}
  if(!peak)peak=memory.peak;else assert(peak==memory.peak);assert(!memory.live);
 }
 reset();
}
int main(int argc,char**argv){assert(argc==2||argc==3);F::rootPath=argv[1];setbuf(stdout,nullptr);if(argc==3){assert(!strcmp(argv[2],"--history-capacity"));historyCapacity();return 0;}relatedArtifacts();hostileArtifacts();syncGate();targetBoundStages();synchronizationCapacity();artifactCustody();writerCuts();reset();puts("Interrupted SD catalog suite PASS");}
