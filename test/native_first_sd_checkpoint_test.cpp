#include "native_first_sd_checkpoint_fixture.h"
static uint64_t w64(const uint8_t*p){uint64_t n=0;for(unsigned i=0;i<8;++i)n|=uint64_t(p[i])<<(8*i);return n;}
[[maybe_unused]] static Bytes&image(Native&n){return n.objects.at(w64(n.root.data()+24));}
static void imported(C&c,Hash&h,bool empty=false){
 using A=RiscExperimental::NativeBaselineAdmission;
 struct Source{bool empty;};Source source{empty};auto hash=h.api();
 A::Sources sources{&source,[](void*){return int(C::Ok);},nullptr,
 [](void*p,uint32_t ns,const A::Selection&s,const A::Rows&rows,A::Witness*w){*w={ns,A::RootState::NeverInitialized,s.kind,s.revision,{}};memcpy(w->fingerprint,s.fingerprint,32);if(static_cast<Source*>(p)->empty)return int(C::Ok);return rows.append(rows.context,"legacy","valuable",8);},
 [](void*,uint32_t,const A::Witness&){return int(C::Ok);}};
 A a(&source,c,{nullptr,[](void*){return true;},[](void*){return uint64_t(1);}},{hash.context,hash.begin,hash.update,hash.finish},sources);
 A::Selection selected;selected.kind=1;selected.revision=7;selected.fingerprint[0]=42;C::Initialization admission;assert(a.admitImported(selected,&admission)==0);assert(c.initializeImportedBaseline(admission)==0);
}
static void knownSeed(Native&n){Files files;seed(n,files);}
static void checkVerified(C&,FirstSd&sd,F&f){sd.runAll(f);if(f.state()!=F::State::Verified)fprintf(stderr,"first failed state=%d reason=%d error=%d\n",int(f.state()),int(f.reason()),f.error());assert(f.state()==F::State::Verified);}
static void happy(){
 for(unsigned kind=0;kind<3;++kind){Native n;Hash h;if(!kind)knownSeed(n);C c(6,n.api(),limits(),C::heap());if(!kind)assert(c.load()==0);else imported(c,h,kind==2);if(kind)edit(c);
  const auto origin=c.baseline().origin();const auto oldGeneration=c.identity().generation;Files files;FirstSd sd(files,&c);F f(c,h.api(),sd.guard(),sd.authority());assert(f.start(sd.volume())==0);sd.run(f);assert(f.state()==F::State::Admitted&&sd.mutations==0&&!c.exportPrepared());assert(f.prepareNative()==0&&c.formatVersion()==4&&c.exportPrepared());
  C::ExportMarker m;assert(c.preparedExport(&m)==0&&m.valid()&&m.firstCreation()&&m.target().generation==oldGeneration+1);assert(m.base().known==(!kind));assert(c.baseline().origin()==origin);assert(c.retireExport(m,m.target())==C::Conflict);
  edit(c,"newer");checkVerified(c,sd,f);assert(f.acknowledgeNative()==0&&f.exitSafe());assert(c.formatVersion()==3&&c.identity().generation==oldGeneration+1&&c.pendingCount()==1&&!c.exportPrepared()&&get(c,"edited")=="newer");
  if(kind==1)assert(get(c,"legacy")=="valuable"&&c.baseline().archiveObligation());
  assert(sd.indexReplacements==1);C reboot(6,n.api(),limits(),C::heap());assert(reboot.load()==0&&get(reboot,"edited")=="newer"&&reboot.pendingCount()==1);
 }
 puts("first create: known lineage, unknown imported/empty baseline, durable-before-pages and newer native edit preservation PASS");
}
static void refusals(){
 for(unsigned mode=0;mode<8;++mode){Native n;knownSeed(n);C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Files files;if(mode==0)addLegacy(files);if(mode==1)addLegacy(files,true);if(mode==2)files["/RTECFG/00000006/unknown"]={1};if(mode==3)files["/RTECFG/00000006/nr-leftover"]={1};if(mode==4)files[Index]={1};if(mode==5)files[std::string(Index)+".risc-replace"]={1};const auto original=files;const auto root=n.root;FirstSd sd(files,&c);Hash h;F f(c,h.api(),sd.guard(),sd.authority());if(mode==6)sd.quiet=false;if(mode==7)sd.entropyOk=false;const int rc=f.start(sd.volume());if(!rc){sd.run(f);if(f.state()==F::State::Admitted)assert(f.prepareNative()!=0);}assert(f.state()!=F::State::Verified&&files==original&&n.root==root&&sd.mutations==0&&!c.exportPrepared());}
 {Native n;C c(6,n.api(),limits(),C::heap());assert(c.initialize(nullptr,0,{})==0);Files files;FirstSd sd(files,&c);Hash h;F f(c,h.api(),sd.guard(),sd.authority());assert(f.start(sd.volume())==C::Conflict&&f.reason()==F::Reason::Unclassified);}
 {Native n;knownSeed(n);C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Files files;FirstSd sd(files,&c);Hash h;F f(c,h.api(),sd.guard(),sd.authority());assert(f.start(sd.volume())==0);sd.run(f);assert(f.state()==F::State::Admitted);edit(c,"late");assert(f.prepareNative()==C::Stale&&sd.mutations==0);}
 puts("first create: legacy/new/unknown/orphan/backup preservation, unclassified provenance, missing entropy/custody and stale capture refusals PASS");
}
static Native armedSeed(){Native n;knownSeed(n);C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Files files;FirstSd sd(files,&c);Hash h;F f(c,h.api(),sd.guard(),sd.authority());assert(f.start(sd.volume())==0);sd.run(f);assert(f.prepareNative()==0);f.cancel();sd.run(f);assert(f.exitSafe());return n;}
static Files expectedFiles(const Native&seed){Native n=seed;C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Files files;FirstSd sd(files,&c);Hash h;F f(c,h.api(),sd.guard(),sd.authority());assert(f.start(sd.volume())==0);checkVerified(c,sd,f);f.cancel();return files;}
static void prefixRecovery(){
 const Native seed=armedSeed();const Files complete=expectedFiles(seed);unsigned cases=0;
 for(const auto&file:complete){if(file.first==Index)continue;for(size_t cut:{size_t(0),size_t(1),file.second.size()/2,file.second.size()-1,file.second.size()}){Native n=seed;Files files;files[file.first]=Bytes(file.second.begin(),file.second.begin()+cut);C c(6,n.api(),limits(),C::heap());assert(c.load()==0);for(unsigned i=0;i<5;++i)edit(c,"newer");FirstSd sd(files,&c);Hash h;F f(c,h.api(),sd.guard(),sd.authority());assert(f.start(sd.volume())==0);checkVerified(c,sd,f);assert(f.acknowledgeNative()==0&&c.pendingCount()==5&&get(c,"edited")=="newer"&&files==complete&&sd.randoms==0);++cases;}}
 for(size_t cut:{size_t(0),size_t(1),size_t(41),size_t(83),size_t(84)}){Native n=seed;Files files=complete;files["/RTECFG/00000006/.replica.first"]=Bytes(files[Index].begin(),files[Index].begin()+cut);files.erase(Index);C c(6,n.api(),limits(),C::heap());assert(c.load()==0);FirstSd sd(files,&c);Hash h;F f(c,h.api(),sd.guard(),sd.authority());assert(f.start(sd.volume())==0);checkVerified(c,sd,f);assert(f.acknowledgeNative()==0&&files==complete);++cases;}
 // Same address with nonprefix bytes never acquires cleanup authority.
 for(unsigned mode=0;mode<4;++mode){Native n=seed;Files files;if(mode==0){auto i=complete.begin();if(i->first==Index)++i;files[i->first]={255};}if(mode==1)files["/RTECFG/00000006/.replica.first"]={255};if(mode==2){files=complete;files[Index][32]^=1;}if(mode==3){files=complete;files["/RTECFG/00000006/.replica.first"]={255};}const Files before=files;C c(6,n.api(),limits(),C::heap());assert(c.load()==0);FirstSd sd(files,&c);Hash h;F f(c,h.api(),sd.guard(),sd.authority());assert(f.start(sd.volume())==0);sd.run(f);assert(f.state()==F::State::Failed&&files==before&&c.exportPrepared()&&c.pendingCount()==1);}
 printf("first create: %u exact marker-owned torn-prefix repairs, reboot with repeated newer edits, stable lineage and nonprefix conflicts PASS\n",cases);
}
static void crashMatrix(){
 const Native seed=armedSeed();Files empty;const Files complete=expectedFiles(seed);unsigned operations=0;
 {Native n=seed;C c(6,n.api(),limits(),C::heap());assert(c.load()==0);FirstSd sd(empty,&c);Hash h;F f(c,h.api(),sd.guard(),sd.authority());assert(f.start(sd.volume())==0);checkVerified(c,sd,f);operations=sd.io;f.cancel();}
 unsigned cases=0;
 for(unsigned crash=0;crash<2;++crash)for(unsigned after=0;after<2;++after)for(unsigned cut=1;cut<=operations;++cut){Native n=seed;Files files;
  try{C c(6,n.api(),limits(),C::heap());assert(c.load()==0);FirstSd sd(files,&c);sd.fault=cut;sd.crash=crash;sd.after=after;Hash h;F f(c,h.api(),sd.guard(),sd.authority());assert(f.start(sd.volume())==0);sd.run(f);}catch(const Sd::Cut&){}
  C reboot(6,n.api(),limits(),C::heap());assert(reboot.load()==0&&reboot.exportPrepared());edit(reboot,"after-reboot");FirstSd sd(files,&reboot);Hash h;F f(reboot,h.api(),sd.guard(),sd.authority());assert(f.start(sd.volume())==0);checkVerified(reboot,sd,f);assert(f.acknowledgeNative()==0&&files==complete&&get(reboot,"edited")=="after-reboot"&&reboot.pendingCount()==1&&sd.randoms==0);++cases;
 }
 printf("first create: %u worker before/after error/power cuts resume with fresh inventory/full readback/close and preserve newer edits PASS\n",cases);
}
static void boundsAndCorruption(){
 const Native seed=armedSeed();unsigned checked=0;
 for(unsigned at:{0u,112u,C::Header+8,C::Header+12,C::Header+64,C::Header+72,C::Header+136,C::Header+204,C::HeaderV3,C::HeaderV3+4,C::HeaderV4}){Native n=seed;auto&b=image(n);b[at]^=1;put32(b.data()+C::Header+252,catalogCrc(b.data()+C::Header,252));put32(b.data()+b.size()-4,catalogCrc(b.data(),b.size()-4));put32(n.root.data()+32,catalogCrc(b.data(),b.size()-4));put32(n.root.data()+44,catalogCrc(n.root.data(),44));C c(6,n.api(),limits(),C::heap());assert(c.load()==C::Io);++checked;}
 for(unsigned at:{C::HeaderV3,C::Header+64}){Native n=seed;auto&b=image(n);put32(b.data()+at,UINT32_MAX);put32(b.data()+C::Header+252,catalogCrc(b.data()+C::Header,252));put32(b.data()+b.size()-4,catalogCrc(b.data(),b.size()-4));put32(n.root.data()+32,catalogCrc(b.data(),b.size()-4));put32(n.root.data()+44,catalogCrc(n.root.data(),44));C c(6,n.api(),limits(),C::heap());assert(c.load()==C::Io);++checked;}
 {Native n;knownSeed(n);const auto original=n.root;const uint32_t oldSize=image(n).size();C c(6,n.api(),{oldSize+256,4096,1024},C::heap());assert(c.load()==0);Files files;FirstSd sd(files,&c);Hash h;F f(c,h.api(),sd.guard(),sd.authority());assert(f.start(sd.volume())==0);sd.run(f);assert(f.prepareNative()==C::NoSpace&&n.root==original&&files.empty());}
 {Native n;knownSeed(n);C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Files files;FirstSd sd(files,&c);sd.space=false;Hash h;F f(c,h.api(),sd.guard(),sd.authority());assert(f.start(sd.volume())==C::NoSpace&&!sd.io&&!c.exportPrepared());}
 {Native n;knownSeed(n);C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Files files;FirstSd sd(files,&c);Hash h;F f(c,h.api(),sd.guard(),sd.authority());assert(f.start(sd.volume())==0);sd.run(f);sd.space=false;assert(f.prepareNative()==C::NoSpace&&!sd.mutations&&!c.exportPrepared());}
 // Repaired CRCs cannot substitute for the target SHA after newer edits.
 {Native n=seed;{C c(6,n.api(),limits(),C::heap());assert(c.load()==0);edit(c,"newer");}auto&b=image(n);b[C::HeaderV4+20]^=1;const uint32_t captured=uint32_t(b[480])|(uint32_t(b[481])<<8)|(uint32_t(b[482])<<16)|(uint32_t(b[483])<<24);put32(b.data()+C::Header+204,catalogCrc(b.data()+C::HeaderV4,captured));put32(b.data()+C::Header+252,catalogCrc(b.data()+C::Header,252));put32(b.data()+b.size()-4,catalogCrc(b.data(),b.size()-4));put32(n.root.data()+32,catalogCrc(b.data(),b.size()-4));put32(n.root.data()+44,catalogCrc(n.root.data(),44));C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Files files;FirstSd sd(files,&c);Hash h;F f(c,h.api(),sd.guard(),sd.authority());assert(f.start(sd.volume())==C::Conflict&&!sd.io&&!sd.mutations);}
 // Bounded root inventory failure and truthful unavailable session both refuse.
 {Native n;knownSeed(n);C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Files files;FirstSd sd(files,&c);for(unsigned i=0;i<129;++i)sd.dirs.insert("/other"+std::to_string(i));Hash h;F f(c,h.api(),sd.guard(),sd.authority());assert(f.start(sd.volume())==0);sd.run(f);assert(f.state()==F::State::Failed&&f.error()==C::NoSpace&&!c.exportPrepared());}
 {Native n;knownSeed(n);C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Files files;FirstSd sd(files,&c);sd.sessionUnavailable=true;Hash h;F f(c,h.api(),sd.guard(),sd.authority());assert(f.start(sd.volume())==0);sd.run(f);assert(f.state()==F::State::Failed&&!sd.mutations);}
 printf("first create: %u repaired-checksum purpose/capture/envelope corruptions, doubled-map native capacity, SD reservation and bounded inventory refusals PASS\n",checked);
}
static void nativeUncertainty(){
 for(unsigned phase=0;phase<2;++phase)for(unsigned applied=0;applied<2;++applied){Native n;knownSeed(n);Files files;
  {C c(6,n.api(),limits(),C::heap());assert(c.load()==0);FirstSd sd(files,&c);Hash h;F f(c,h.api(),sd.guard(),sd.authority());assert(f.start(sd.volume())==0);sd.run(f);
   if(!phase){n.failAck=true;n.applyAck=applied;assert(f.prepareNative()==C::Uncertain&&files.empty()&&!sd.mutations&&!c.ready());}
   else {assert(f.prepareNative()==0);edit(c,"newer");checkVerified(c,sd,f);n.failAck=true;n.applyAck=applied;assert(f.acknowledgeNative()==C::Uncertain&&!c.ready());}
  }
  n.failAck=false;C reboot(6,n.api(),limits(),C::heap());assert(reboot.load()==0);if(!phase||!applied){FirstSd sd(files,&reboot);Hash h;F f(reboot,h.api(),sd.guard(),sd.authority());assert(f.start(sd.volume())==0);checkVerified(reboot,sd,f);assert(f.acknowledgeNative()==0);}
  assert(!reboot.exportPrepared()&&reboot.identity().generation==8&&reboot.pendingCount()==(phase?1:0));if(phase)assert(get(reboot,"edited")=="newer");
 }
 {Native n=armedSeed();C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Files files;FirstSd sd(files,&c);Hash h;F f(c,h.api(),sd.guard(),sd.authority());assert(f.start(sd.volume())==0);checkVerified(c,sd,f);sd.retained=true;assert(f.acknowledgeNative()==C::Retained&&f.state()==F::State::Retained&&!f.exitSafe()&&c.exportPrepared()&&c.pendingCount()==1);}
 for(unsigned phase=0;phase<2;++phase){Native n;knownSeed(n);C c(6,n.api(),limits(),C::heap());assert(c.load()==0);Files files;FirstSd sd(files,&c);Hash h;F f(c,h.api(),sd.guard(),sd.authority());assert(f.start(sd.volume())==0);sd.run(f);if(phase){assert(f.prepareNative()==0);checkVerified(c,sd,f);}const auto root=n.root;sd.retainOnQuiet=true;sd.quiet=false;assert((phase?f.acknowledgeNative():f.prepareNative())==C::Retained&&f.state()==F::State::Retained&&!f.exitSafe()&&n.root==root);}
 puts("first create: ambiguous native arm/ack old-or-new reload and retained verification refuses acknowledgement PASS");
}
static void emptyCheckpoint(){
 Native n;C c(6,n.api(),limits(),C::heap());Hash h;imported(c,h,true);Files files;FirstSd sd(files,&c);F f(c,h.api(),sd.guard(),sd.authority());assert(f.start(sd.volume())==0);checkVerified(c,sd,f);assert(f.acknowledgeNative()==0&&files.size()==1&&files.count(Index)&&c.identity().generation==1&&!c.pendingCount());
 puts("first create: explicitly selected complete empty dataset publishes only index PASS");
}
int main(){happy();refusals();prefixRecovery();crashMatrix();boundsAndCorruption();nativeUncertainty();emptyCheckpoint();puts("EXPERIMENTAL STRICT-GUARD HOST PROOF ONLY: no production binding or hardware qualification");}
