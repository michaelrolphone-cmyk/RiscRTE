#include "native_settings_catalog_fixture.h"
#include "experimental/native_replica/NativeRebaseCandidate.h"
static void basics(){for(unsigned mode=0;mode<7;++mode){Native n;Files f;C c(6,n.api(),limits(),C::heap());if(mode>=3){save(f,{},0,identity({},0));}if(mode>=4)addLegacy(f);if(mode>=5)addLegacy(f,true);if(mode==6)f["/RTECFG/00000006/orphan"]={1};DirectorySd sd(f);if(mode==1)sd.dirs.insert("/RTECFG");if(mode==2){sd.dirs.insert("/RTECFG");sd.dirs.insert("/RTECFG/00000006");}const Files before=f;Hash h;CatalogAlloc a;K k(c,h.api(),sd.guard(),a.api());assert(k.start(sd.volume())==0);sd.run(k);K::Report out;if(k.state()!=K::State::Verified)fprintf(stderr,"mode%u state%d reason%d err%d\n",mode,int(k.state()),int(k.reason()),k.error());assert(k.take(&out)==0&&out.valid()&&out.namespaceId()==6&&k.exitSafe());assert(out.vacant()==(mode<3));assert(out.stableInventory());if(mode>=3)assert(out.slot(0)->content==K::Content::Valid);if(mode>=4)assert(out.slot(1)->content==K::Content::Valid);if(mode>=5)assert(out.slot(2)->content==K::Content::Valid);assert(f==before&&!n.calls&&!a.live);}}
static void forbidden(const DirectorySd&s){for(const auto&op:s.operations)assert(op!="open-write"&&op!="write"&&op!="remove"&&op!="sync"&&op.find("replace-")!=0);assert(!s.mutations&&!s.indexReplacements);}
static void limitsAndArtifacts(){
 for(unsigned mode=0;mode<12;++mode){Native n;Files f;seed(n,f);addLegacy(f);addLegacy(f,true);C c(6,n.api(),limits(),C::heap());assert(c.load()==0);const unsigned calls=n.calls;DirectorySd sd(f);K::Limits limit{};
  if(mode==0){for(unsigned i=0;i<12;++i)f["/RTECFG/00000006/unknown"+std::to_string(i)]={2};limit.entries=4;}
  if(mode==1)limit.nameBytes=3;
  if(mode>=2&&mode<=6)sd.malformed=mode-1;
  if(mode==7){sd.reverse=true;sd.dots=true;}
  if(mode==8){sd.maskOpen=true;sd.maskMetadata=true;}
  if(mode==9){f["/RTECFG/00000006/.replica.next"]={2};f["/RTECFG/00000006/nr-orphan"]={2};f["/RTECFG/00000006/kv-orphan"]={3};f["/RTECFG/00000006/unknown"]={7};sd.dirs.insert("/RTECFG/00000006/subdir");}
  if(mode==10)sd.injectOnSecond=true;
  if(mode==11)f[Index][0]^=1;
  Hash h;CatalogAlloc a;K k(c,h.api(),sd.guard(),a.api());assert(k.start(sd.volume(),limit)==0);sd.run(k);K::Report out;assert(k.take(&out)==0&&!out.vacant()&&!a.live);assert(out.slot(1)->content==K::Content::Valid&&out.slot(2)->content==K::Content::Valid);assert(out.slot(0)->content==(mode==11?K::Content::Invalid:K::Content::Valid));
  if(mode<2)assert(out.initialDirectory()==K::Directory::Incomplete&&!out.stableInventory());
  if(mode>=2&&mode<=6)assert(out.initialDirectory()==K::Directory::Invalid&&!out.stableInventory());
  if(mode==7)assert(out.stableInventory());
  if(mode==8)assert(out.initialDirectory()==K::Directory::Unreadable&&!out.stableInventory());
  if(mode==9){const auto art=out.artifacts();assert(art.staging==1&&art.orphanPages==2&&art.unknown==1&&art.directories==1&&art.selectedPages==3);}
  if(mode==10)assert(!out.stableInventory()&&out.artifacts().unknown==1);
  assert(n.calls==calls&&c.pendingCount()==1&&get(c,"edited")=="local");forbidden(sd);
 }
 // Capacity refuses that candidate, while retaining independently valid legacy.
 Native n;Files f;save(f,mapRows({"k0","k1","k2"}),3,identity(mapRows({"k0","k1","k2"}),3));addLegacy(f);C small(6,n.api(),{C::Header+4+52,4096,1024},C::heap());DirectorySd sd(f);Hash h;K k(small,h.api(),sd.guard());assert(k.start(sd.volume())==0);sd.run(k);K::Report out;assert(k.take(&out)==0&&!out.vacant()&&out.slot(0)->content==K::Content::CapacityExceeded&&out.slot(1)->content==K::Content::Valid);
 puts("Catalog: complete/partial bounded inventory, valid metadata retained after limit, malformed names/duplicates, parent absence check, order independence and artifact classification PASS");
}
static void recovery(){for(unsigned source=0;source<3;++source)for(bool backupOnly:{false,true}){Native n;Files f;save(f,{},0,identity({},0));addLegacy(f);addLegacy(f,true);const std::string path=std::string("/RTECFG/00000006/")+(source==0?"replica.index":source==1?"settings.index":"settings.kv");const auto original=f;f[path+".risc-replace"]=f.at(path);if(backupOnly)f.erase(path);const auto before=f;DirectorySd sd(f);sd.reverse=true;Hash h;C c(6,n.api(),limits(),C::heap());K k(c,h.api(),sd.guard());assert(k.start(sd.volume())==0);sd.run(k);K::Report out;assert(k.take(&out)==0&&!out.vacant()&&out.stableInventory());assert(out.initialBackups()==(1u<<source));assert(out.recovered()==(backupOnly?1u<<source:0));assert(out.finalBackups()==(backupOnly?0:1u<<source));assert(f==(backupOnly?original:before));for(unsigned i=0;i<3;++i)assert(out.slot(i)->content==K::Content::Valid);forbidden(sd);}
 puts("Catalog: exact fixed-name backup-only recovery, present+backup preservation and no unknown artifact recovery PASS");
}
static unsigned successful(Native&n,Files&f){C c(6,n.api(),limits(),C::heap());assert(c.load()==0);DirectorySd sd(f);Hash h;K k(c,h.api(),sd.guard());assert(k.start(sd.volume())==0);unsigned steps=sd.run(k);K::Report out;assert(k.take(&out)==0&&out.slot(0)->content==K::Content::Valid&&!out.vacant());return steps;}
static void boundaries(){Native initial;Files original;seed(initial,original);addLegacy(original);addLegacy(original,true);const unsigned steps=successful(initial,original);unsigned cases=0;
 for(bool queued:{false,true})for(unsigned mode=0;mode<4;++mode)for(unsigned cut=0;cut<=steps;++cut){Native n=initial;Files f=original;{C c(6,n.api(),limits(),C::heap());assert(c.load()==0);const unsigned calls=n.calls;DirectorySd sd(f);Hash h;CatalogAlloc a;K k(c,h.api(),sd.guard(),a.api());assert(k.start(sd.volume())==0);for(unsigned i=0;i<cut&&k.state()==K::State::Running;++i)sd.tick(k);if(queued&&k.state()==K::State::Running)sd.tick(k,false);
  if(mode==0){k.cancel();sd.run(k);assert(k.state()==K::State::Cancelled&&k.exitSafe());}
  if(mode==1||mode==2){if(mode==1)sd.available=false;else sd.lease=false;if(k.state()==K::State::Verified){K::Report out;assert(k.take(&out)!=0&&!out.valid());}else sd.run(k);assert(k.state()==K::State::Failed&&k.exitSafe());}
  assert(n.calls==calls&&c.pendingCount()==1&&get(c,"edited")=="local"&&f==original);forbidden(sd);
 }unchanged(n);successful(n,f);++cases;}
 printf("Catalog: %u queued/completed scan/reader cancel/removal/export/restart boundaries preserve native intents and files PASS\n",cases);
}
static void faults(){Native initial;Files original;seed(initial,original);addLegacy(original);addLegacy(original,true);unsigned cases=0;
 for(bool backup:{false,true}){Files source=original;if(backup){source[std::string(Index)+".risc-replace"]=source[Index];source.erase(Index);}unsigned io=0;{Files f=source;DirectorySd sd(f);Hash h;C c(6,initial.api(),limits(),C::heap());assert(c.load()==0);K k(c,h.api(),sd.guard());assert(k.start(sd.volume())==0);sd.run(k);assert(k.state()==K::State::Verified);io=sd.io;k.cancel();}
  for(bool crash:{false,true})for(bool after:{false,true})for(unsigned cut=1;cut<=io;++cut){Native n=initial;Files f=source;{C c(6,n.api(),limits(),C::heap());assert(c.load()==0);const unsigned calls=n.calls;DirectorySd sd(f);sd.fault=cut;sd.crash=crash;sd.after=after;Hash h;CatalogAlloc a;K k(c,h.api(),sd.guard(),a.api());assert(k.start(sd.volume())==0);try{sd.run(k);}catch(const Sd::Cut&){}assert(sd.hit&&n.calls==calls&&c.pendingCount()==1&&get(c,"edited")=="local");assert(f==source||f==original);forbidden(sd);K::Report out;if(k.state()==K::State::Verified){assert(k.take(&out)==0&&!out.vacant());}if(k.state()==K::State::Retained){auto before=sd.io;k.cancel();for(unsigned i=0;i<4;++i)sd.tick(k,false);assert(!k.exitSafe()&&k.state()==K::State::Retained&&k.take(&out)==C::Stale&&sd.io==before);}}
   unchanged(n);successful(n,f);assert(f==original);++cases;
  }
 }
 printf("Catalog: %u reached before/after directory/file/recovery/checked-close faults and power cuts; retained custody stays sticky PASS\n",cases);
}
static void custodyAndAdmission(){
 for(unsigned mode=0;mode<7;++mode){Native n;Files f;DirectorySd sd(f);C c(6,n.api(),limits(),C::heap());Hash h;CatalogAlloc a;K k(c,h.api(),sd.guard(),a.api());
  if(mode==0){sd.lease=false;assert(k.start(sd.volume())==C::Conflict);sd.lease=true;}
  if(mode==1){sd.nativeWorker=false;assert(k.start(sd.volume())==C::Busy);sd.nativeWorker=true;}
  if(mode==2){a.failAt=1;assert(k.start(sd.volume())==C::NoMemory);a.failAt=0;}
  if(mode==3)sd.sessionUnavailable=true; // Exact X4 owner-observation behavior.
  assert(k.start(sd.volume())==0);sd.run(k);K::Report out;
  if(mode==3){assert(k.state()==K::State::Failed&&k.reason()==K::Reason::MediaUnavailable&&k.exitSafe()&&k.take(&out)==C::Stale&&!out.valid()&&f.empty());continue;}
  assert(k.state()==K::State::Verified);
  if(mode>=4){sd.retained=true;sd.lease=false;if(mode==4)k.cancel();else assert(k.take(&out)==C::Retained);assert(!out.valid()&&k.state()==K::State::Retained&&!k.exitSafe());k.cancel();assert(k.state()==K::State::Retained);}
  else {assert(k.take(&out)==0&&out.vacant()&&k.reason()==K::Reason::None);}
 }
 // Every allocation site fails safely without losing native state or sources.
 for(unsigned fail=1;fail<=4;++fail){Native n;Files f;seed(n,f);addLegacy(f);addLegacy(f,true);const auto before=f;DirectorySd sd(f);C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Hash h;CatalogAlloc a;a.failAt=fail;K k(c,h.api(),sd.guard(),a.api());const int rc=k.start(sd.volume());if(rc==0){sd.run(k);assert(k.state()==K::State::Failed&&k.exitSafe());}else assert(rc==C::NoMemory);assert(f==before&&c.pendingCount()==1);}
 puts("Catalog: retry/worker/allocation admission, X4 active-session UNAVAILABLE refusal, late retained+lease transfer/cancel and allocation wiping PASS");
}
static void emptyFaults(){Native initial;Files ignored;seed(initial,ignored);unsigned cases=0;
 for(unsigned shape=0;shape<4;++shape){Files original;if(shape==3)original["/RTECFG/00000006/unknown"]={9};auto configure=[&](DirectorySd&s){if(shape>=1)s.dirs.insert("/RTECFG");if(shape>=2)s.dirs.insert("/RTECFG/00000006");};unsigned io=0;std::vector<std::string> operations;
  {DirectorySd sd(original);configure(sd);Hash h;C c(6,initial.api(),limits(),C::heap());assert(c.load()==0);K k(c,h.api(),sd.guard());assert(k.start(sd.volume())==0);sd.run(k);K::Report out;assert(k.take(&out)==0&&out.vacant()==(shape<3));io=sd.io;operations=sd.operations;}
  for(bool crash:{false,true})for(bool after:{false,true})for(unsigned cut=1;cut<=io;++cut){Native n=initial;Files f=original;{DirectorySd sd(f);configure(sd);sd.fault=cut;sd.crash=crash;sd.after=after;Hash h;C c(6,n.api(),limits(),C::heap());assert(c.load()==0);unsigned calls=n.calls;CatalogAlloc a;K k(c,h.api(),sd.guard(),a.api());assert(k.start(sd.volume())==0);try{sd.run(k);}catch(const Sd::Cut&){}assert(sd.hit&&f==original&&n.calls==calls&&c.pendingCount()==1);forbidden(sd);if(k.state()==K::State::Verified){K::Report out;assert(k.take(&out)==0);if(shape==3)assert(!out.vacant());if(out.vacant()){assert(out.stableInventory());for(unsigned i=0;i<3;++i)assert(out.slot(i)->content==K::Content::Missing);}if(operations[cut-1]=="dir-next"||operations[cut-1]=="dir-error"||operations[cut-1]=="dir-close"||operations[cut-1]=="metadata"||operations[cut-1]=="recover")assert(!out.vacant());}if(k.state()==K::State::Retained){k.cancel();assert(k.state()==K::State::Retained&&!k.exitSafe());}}
   unchanged(n);++cases;
  }
 }
 printf("Catalog: %u absent-root/parent, empty-namespace and artifact-only faults/power cuts refuse unproved vacancy without native loss PASS\n",cases);
}
struct CatalogRunning {
 static CatalogRunning*active;DirectorySd&sd;K*job=nullptr;bool fired=false;bool(*next)(void*,uint32_t,risc_storage_dirent_v1*)=nullptr;
 explicit CatalogRunning(DirectorySd&s):sd(s){assert(!active);active=this;auto&v=*const_cast<risc_storage_volume_api_v1*>(s.adapter.volume);next=v.dir_next;v.dir_next=[](void*p,uint32_t h,risc_storage_dirent_v1*out){auto&r=*active;if(!r.fired){r.fired=true;r.job->cancel();for(unsigned i=0;i<3;++i){const unsigned before=r.sd.io;r.job->step();K::Report candidate;assert(r.job->state()==K::State::Running&&!r.job->exitSafe()&&r.job->take(&candidate)==C::Stale&&r.sd.io==before);}}return r.next(p,h,out);};}
 ~CatalogRunning(){active=nullptr;}
};
CatalogRunning*CatalogRunning::active=nullptr;
struct CatalogBusy {
 static CatalogBusy*active;unsigned begins=0,submits=0,ends=0;int32_t(*begin)(void*,uint64_t*)=nullptr;int32_t(*submit)(void*,uint64_t,const risc_storage_async_request_v1*,uint64_t*)=nullptr;int32_t(*end)(void*,uint64_t)=nullptr;
 explicit CatalogBusy(DirectorySd&s){assert(!active);active=this;auto&a=s.download.base.base;begin=a.begin;submit=a.submit;end=a.end;a.begin=[](void*p,uint64_t*t){auto&b=*active;return ++b.begins<=3?int32_t(RISC_STORAGE_ASYNC_BUSY):b.begin(p,t);};a.submit=[](void*p,uint64_t t,const risc_storage_async_request_v1*q,uint64_t*op){auto&b=*active;return ++b.submits<=3?int32_t(RISC_STORAGE_ASYNC_BUSY):b.submit(p,t,q,op);};a.end=[](void*p,uint64_t t){auto&b=*active;if(++b.ends<=4)return int32_t(b.ends%2?RISC_STORAGE_ASYNC_PENDING:RISC_STORAGE_ASYNC_BUSY);return b.end(p,t);};}
 ~CatalogBusy(){active=nullptr;}
};
CatalogBusy*CatalogBusy::active=nullptr;
static void runningAndLimits(){
 Native n;Files f;DirectorySd sd(f);sd.dirs.insert("/RTECFG");sd.dirs.insert("/RTECFG/00000006");C c(6,n.api(),limits(),C::heap());Hash h;
 {CatalogRunning running(sd);K k(c,h.api(),sd.guard());running.job=&k;assert(k.start(sd.volume())==0);sd.run(k);assert(running.fired&&k.state()==K::State::Cancelled&&k.exitSafe()&&sd.dirCloses==1);}
 {DirectorySd sd(f);CatalogBusy busy(sd);Hash h;K k(c,h.api(),sd.guard());assert(k.start(sd.volume())==0);while(k.state()==K::State::Running){K::Report out;assert(k.take(&out)==C::Stale&&!out.valid()&&!k.exitSafe());sd.tick(k);}K::Report out;assert(k.take(&out)==0&&out.vacant()&&busy.begins>3&&busy.submits>3&&busy.ends>4);}
 // Parent scan exhaustion cannot prove absence, even with three Missing slots.
 {DirectorySd sd(f);for(unsigned i=0;i<5;++i)sd.dirs.insert("/other"+std::to_string(i));Hash h;K k(c,h.api(),sd.guard());assert(k.start(sd.volume(),{3,1000})==0);sd.run(k);K::Report out;assert(k.take(&out)==0&&!out.vacant()&&!out.stableInventory()&&out.initialDirectory()==K::Directory::Incomplete);for(unsigned i=0;i<3;++i)assert(out.slot(i)->content==K::Content::Missing);}
 // A literal RTECFG file obstructs the namespace; it never proves absence.
 {Files f;f["/RTECFG"]={7};DirectorySd sd(f);Hash h;K k(c,h.api(),sd.guard());assert(k.start(sd.volume())==0);sd.run(k);K::Report out;assert(k.take(&out)==0&&!out.vacant()&&out.initialDirectory()==K::Directory::Unreadable);}
 // RETAINED hidden by a session-masked observation must be latched after end.
 {DirectorySd sd(f);Hash h;K k(c,h.api(),sd.guard());assert(k.start(sd.volume())==0);sd.tick(k,false);assert(sd.adapter.session);sd.retained=true;sd.sessionUnavailable=true;k.cancel();sd.run(k);assert(k.state()==K::State::Retained&&!k.exitSafe());}
 // A partial inventory cannot infer that a backup was the only prior source.
 {Native local;Files f;seed(local,f);addLegacy(f);addLegacy(f,true);f[std::string(Index)+".risc-replace"]=f[Index];f.erase(Index);DirectorySd sd(f);Hash h;C core(6,local.api(),limits(),C::heap());assert(core.load()==0);K k(core,h.api(),sd.guard());assert(k.start(sd.volume(),{4,8192})==0);sd.run(k);K::Report out;assert(k.take(&out)==0&&!out.vacant()&&!out.stableInventory()&&!out.recovered()&&out.slot(0)->content==K::Content::Valid);}
 // Sensitive identity/digest metadata is physically wiped on Report destruction.
 {Files f;save(f,{},0,identity({},0));DirectorySd sd(f);Hash h;K k(c,h.api(),sd.guard());assert(k.start(sd.volume())==0);sd.run(k);alignas(K::Report) uint8_t bytes[sizeof(K::Report)];auto*out=new(bytes)K::Report;assert(k.take(out)==0&&out->slot(0)->identity.known);out->~Report();for(uint8_t b:bytes)assert(!b);}
 assert(!n.calls);puts("Catalog: actual Running cancellation/checked directory close, BUSY/PENDING custody, parent budget/obstruction and post-end retained priority PASS");
}
static void prefixWiping(){
 // Prefixes contain the complete profile-derived digest, so wipe them as soon
 // as classification stops, including cancellation/retention and destruction.
 for(unsigned mode=0;mode<4;++mode){Native n;Files f;seed(n,f,0);C c(6,n.api(),limits(),C::heap());assert(c.load()==0);DirectorySd sd(f);Hash h;alignas(K) uint8_t bytes[sizeof(K)];K*k=new(bytes)K(c,h.api(),sd.guard());assert(k->start(sd.volume())==0);
  const auto id=c.identity();char digest[65]{};for(unsigned i=0;i<32;++i)std::snprintf(digest+2*i,3,"%02x",unsigned(id.digest[i]));auto has=[&](){return std::search(bytes,bytes+sizeof bytes,reinterpret_cast<const uint8_t*>(digest),reinterpret_cast<const uint8_t*>(digest)+64)!=bytes+sizeof bytes;};
  while(k->state()==K::State::Running&&!has()){sd.tick(*k);}assert(k->state()==K::State::Running&&has());
  if(mode==0){sd.run(*k);assert(k->state()==K::State::Verified&&!has());K::Report report;assert(k->take(&report)==0&&!has());}
  if(mode==1){k->cancel();sd.run(*k);assert(k->state()==K::State::Cancelled&&!has());}
  if(mode==2){sd.retained=true;sd.run(*k);assert(k->state()==K::State::Retained&&!has()&&!k->exitSafe());}
  k->~K();assert(!has());C::wipe(digest,sizeof digest);
 }
 puts("Catalog: sensitive classification prefixes wiped after summary, transfer, cancel, retention and destruction PASS");
}
static void memoryBounds(){
 for(unsigned limit:{128u,256u}){Native n;Files f;seed(n,f);addLegacy(f);addLegacy(f,true);C c(6,n.api(),limits(),C::heap());assert(c.load()==0);DirectorySd sd(f);Hash h;CatalogAlloc a;K k(c,h.api(),sd.guard(),a.api());assert(k.start(sd.volume(),{limit,limit==128?8192u:32768u})==0);const size_t inventory=a.live;assert(inventory==2*limit*K::entryStorageBytes());sd.run(k);K::Report out;assert(k.take(&out)==0&&!out.vacant()&&!a.live&&a.peak==inventory+65512);printf("Catalog memory: entries=%u inventory=%zu peak_allocated=%zu catalog_object=%zu report_object=%zu entry=%zu child_storage=%zu bytes (host ABI)\n",limit,inventory,a.peak,sizeof(K),sizeof(K::Report),K::entryStorageBytes(),K::childStorageBytes());}
}

// Explicitly synthetic selected bytes for read-only catalog format/capacity
// tests. This fake Native backend has no initializer receipt capability and
// does not qualify FreshEmpty admission; the checked-store suite covers that.
static void loadSyntheticV3(C&core,Native&native){
 assert(native.root.empty()&&native.objects.empty()&&native.next==1);
 Bytes image(484);memcpy(image.data(),"RTENR03",8);put32(image.data()+8,6);put32(image.data()+16,1);put32(image.data()+108,image.size());
 auto*b=image.data()+C::HeaderV2;memcpy(b,"RTENBP1",8);put32(b+8,6);put32(b+12,uint32_t(C::Origin::FreshEmpty));put32(b+92,catalogCrc(b,92));put32(image.data()+480,catalogCrc(image.data(),480));
 native.root=Bytes(48);auto*r=native.root.data();memcpy(r,"RTENROOT",8);put32(r+8,6);put32(r+12,image.size());put32(r+16,1);put32(r+24,1);put32(r+32,catalogCrc(image.data(),480));put32(r+44,catalogCrc(r,44));native.objects[native.next++]=image;
 assert(core.load()==C::Ok&&core.formatVersion()==3&&core.headerSize()==480&&core.baseline().origin()==C::Origin::FreshEmpty);
}
static Bytes catalogMap(uint32_t bytes,uint32_t*count){
 Bytes map;*count=0;while(bytes){assert(bytes>=21);uint32_t row=std::min(bytes,2068u);if(bytes>row&&bytes-row<21)row-=21-(bytes-row);const uint32_t at=map.size();map.resize(at+row);char key[16];snprintf(key,sizeof key,"bound%04u",(*count)++);strcpy(reinterpret_cast<char*>(map.data()+at),key);put32(map.data()+at+16,row-20);memset(map.data()+at+20,'v',row-20);bytes-=row;}return map;
}
static void storeCatalogLegacy(Files&files,const Bytes&map,uint32_t count){
 Bytes b(132+map.size());memcpy(b.data(),"RISCKV2",8);put32(b.data()+8,6);put32(b.data()+16,7);strcpy(reinterpret_cast<char*>(b.data()+24),"shared-00000006");put32(b.data()+120,count);put32(b.data()+124,b.size());memcpy(b.data()+128,map.data(),map.size());put32(b.data()+b.size()-4,catalogCrc(b.data(),b.size()-4));
 Bytes index(28);memcpy(index.data(),"RTEKVP2",8);put32(index.data()+8,b.size());put32(index.data()+12,7);put32(index.data()+20,catalogCrc(b.data(),b.size()));put32(index.data()+24,catalogCrc(index.data(),24));files["/RTECFG/00000006/settings.index"]=index;for(size_t at=0;at<b.size();at+=4096){char path[96];snprintf(path,sizeof path,"/RTECFG/00000006/kv-0000000000000007-%08x",unsigned(at/4096));files[path]={b.begin()+at,b.begin()+std::min(at+4096,b.size())};}
}
static void v3CapacityAndPreconstruction(){using R=K::R;using L=K::L;unsigned cases=0;
 for(bool preconstructed:{false,true})for(unsigned extra:{0u,1u}){Native n;C core(6,n.api(),limits(),C::heap());Files files;uint32_t count=0;auto map=catalogMap(65028+extra,&count);save(files,map,count,identity(map,count));storeCatalogLegacy(files,map,count);addLegacy(files,true);const auto originalFiles=files;DirectorySd sd(files);Hash h;CatalogAlloc alloc;
  if(!preconstructed)loadSyntheticV3(core,n);
  K catalog(core,h.api(),sd.guard(),alloc.api());R replica(core,h.api(),sd.guard());L legacy(core,h.api(),sd.guard());
  if(preconstructed)loadSyntheticV3(core,n);
  edit(core);const unsigned nativeCalls=n.calls;const auto root=n.root;const auto objects=n.objects;const size_t inventory=2*K::Limits{}.entries*K::entryStorageBytes();
  assert(catalog.start(sd.volume())==C::Ok);sd.run(catalog);K::Report report;assert(catalog.take(&report)==C::Ok&&catalog.exitSafe()&&!report.vacant()&&report.stableInventory());const auto expected=extra?K::Content::CapacityExceeded:K::Content::Valid;assert(report.slot(0)->content==expected&&report.slot(1)->content==expected&&report.slot(2)->content==K::Content::Valid);if(!extra)assert(report.slot(0)->mapBytes==65028&&report.slot(1)->mapBytes==65028);assert(!alloc.live&&alloc.peak==inventory+65160);
  assert(replica.start(sd.volume())==C::Ok);sd.run(replica);R::Candidate replicaMap;if(extra)assert(replica.state()==R::State::Failed&&replica.reason()==R::Reason::Capacity&&replica.exitSafe());else {assert(replica.take(&replicaMap)==C::Ok&&replicaMap.mapSize()==65028);RiscExperimental::NativeRebaseCandidate held(core,6,{},C::heap());assert(held.begin({replicaMap.namespaceId(),replicaMap.count(),replicaMap.mapSize(),replicaMap.map(),replicaMap.identity()})==C::NoSpace&&!held.held());}replicaMap.reset();
  assert(legacy.start(sd.volume(),L::Source::PagedV2)==C::Ok);sd.run(legacy);L::Candidate legacyMap;if(extra)assert(legacy.state()==L::State::Failed&&legacy.reason()==L::Reason::Capacity&&legacy.exitSafe());else assert(legacy.take(&legacyMap)==C::Ok&&legacyMap.mapSize()==65028);legacyMap.reset();
  assert(n.calls==nativeCalls&&n.root==root&&n.objects==objects&&files==originalFiles&&core.pendingCount()==1&&get(core,"edited")=="local"&&core.baseline().origin()==C::Origin::FreshEmpty);forbidden(sd);++cases;
 }
 // Small V1 single-file input must honor the live v3 target budget as well.
 for(bool preconstructed:{false,true})for(unsigned budget:{51u,52u}){Native n;C core(6,n.api(),{484+budget,4096,1024},C::heap());Files files;addLegacy(files,true);DirectorySd sd(files);Hash h;
  if(!preconstructed)loadSyntheticV3(core,n);
  K catalog(core,h.api(),sd.guard());L legacy(core,h.api(),sd.guard());if(preconstructed)loadSyntheticV3(core,n);const unsigned calls=n.calls;const auto root=n.root;
  assert(catalog.start(sd.volume())==C::Ok);sd.run(catalog);K::Report report;assert(catalog.take(&report)==C::Ok&&!report.vacant()&&report.slot(2)->content==(budget==51?K::Content::CapacityExceeded:K::Content::Valid));
  assert(legacy.start(sd.volume(),L::Source::SingleFile)==C::Ok);sd.run(legacy);L::Candidate out;if(budget==51)assert(legacy.state()==L::State::Failed&&legacy.reason()==L::Reason::Capacity);else assert(legacy.take(&out)==C::Ok&&out.mapSize()==52);assert(n.calls==calls&&n.root==root);forbidden(sd);++cases;
 }
 printf("Catalog union: %u RTENR03 capacity/preconstruction cases; exact 65,028-byte replica/V2 and 52-byte V1 budgets, one-byte excess refusals, preserved groups prevent over-capacity adoption, no-native-write invariants and bounded v3 peak PASS\n",cases);
}

int main(){setbuf(stdout,nullptr);basics();limitsAndArtifacts();recovery();boundaries();faults();custodyAndAdmission();emptyFaults();runningAndLimits();prefixWiping();memoryBounds();v3CapacityAndPreconstruction();puts("DISCONNECTED CATALOG ONLY: historical evidence; no creation/adoption permit, native mutation, defaults, cleanup, production activation or hardware claim");}
