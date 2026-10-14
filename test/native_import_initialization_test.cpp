// Reuse the real NVS collector/store/AppData fixture and its fault wrappers.
#define main legacy_sources_test_main
#include "native_nvs_legacy_sources_test.cpp"
#undef main
#include <openssl/sha.h>
namespace ImportRecoveryTest {
using Metadata=decltype(F::nvs);
struct Bundle {
 Hash sourceHash,mapHash;Sources sources;A factory;
 explicit Bundle(F::Fixture&f,const uint8_t*owner=Owner,const char*legacy=LegacyOwner,uint32_t capacity=30000,C::Allocator allocation=C::heap())
  :sources(6,owner,legacy,capacity,f.inspector,sourceHash.api(),hooks(),allocation),factory(&sources,f.core,guard(),mapHash.api(),sources.sources(),allocation){}
};
static Bytes root(){auto s=F::nvs.find("rnr00000006");return s==F::nvs.end()||!s->second.count("root")?Bytes{}:s->second.at("root");}
static uint64_t wide(const uint8_t*p){return Old::word(p)|(uint64_t(Old::word(p+4))<<32);}
static uint64_t counter(){auto s=F::nvs.find("rnr00000006");return s==F::nvs.end()||!s->second.count("next")?0:wide(s->second.at("next").data()+8);}
static void sequence(uint64_t n){Bytes b(24);memcpy(b.data(),"RTENSEQ",8);put(b.data()+8,uint32_t(n));put(b.data()+12,uint32_t(n>>32));repair(b);F::nvs["rnr00000006"]["next"]=b;}
static Metadata originals(){auto out=F::nvs;out.erase("rnr00000006");return out;}
static Rows expected(){return {{"added",{6}},{"journal",{4}},{"original",{5}},{"untouched",{2}}};}
static void file(const char*name,const Bytes&bytes){const auto folder=F::rootPath+"/n00000006";std::filesystem::create_directories(folder);std::ofstream out(folder+"/"+name,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());assert(out.good());}
static std::map<std::string,Bytes> files(){std::map<std::string,Bytes> out;for(auto&e:std::filesystem::recursive_directory_iterator(F::rootPath))if(e.is_regular_file()){std::ifstream in(e.path(),std::ios::binary);out[e.path().lexically_relative(F::rootPath).string()]=Bytes(std::istreambuf_iterator<char>(in),{});}return out;}
static A::Witness inventory(Bundle&b){A::Witness w;assert(b.sources.inventory(&w)==C::Ok);return w;}
static int initialize(F::Fixture&f,Bundle&b,bool resume,const A::Witness&w){C::Initialization a;int rc=resume?b.factory.admitImportedResume(&a):b.factory.admitImported(selection(w),&a);if(!rc)rc=f.core.initializeImportedBaseline(a);f.store.finishOperation();return rc;}
static void verify(F::Fixture&f,const A::Witness&w,const Rows&rows=expected()){
 assert(f.core.ready()&&f.core.formatVersion()==3&&f.core.revision()==1&&!f.core.pendingCount());
 const auto baseline=f.core.baseline();assert(baseline.origin()==C::Origin::ImportedLegacy&&baseline.archiveObligation()&&baseline.count()==rows.size()&&!memcmp(baseline.sourceFingerprint(),w.fingerprint,32));
 for(auto&r:rows){std::string got;assert(F::get(f.core,r.first.c_str(),&got)==C::Ok&&Bytes(got.begin(),got.end())==r.second);}
 C::Snapshot snapshot;assert(f.core.captureSnapshot(&snapshot)==C::Ok&&snapshot.count()==rows.size());uint32_t at=0;for(auto&r:rows){assert(!strcmp(reinterpret_cast<const char*>(snapshot.map()+at),r.first.c_str())&&Old::word(snapshot.map()+at+16)==r.second.size()&&!memcmp(snapshot.map()+at+20,r.second.data(),r.second.size()));at+=20+r.second.size();}assert(at==snapshot.mapSize());
}
static A::Witness receiptOnly(bool existingRnr=true){
 reset();seed();if(!existingRnr)F::nvs.erase("rnr00000006");A::Witness w{};const auto original=originals();
 {F::Fixture f;Bundle b(f);w=inventory(b);C::Initialization a;assert(b.factory.admitImported(selection(w),&a)==C::Ok);
  F::fault="nvs-set";F::cut=1;F::nativeCalls=0;F::after=true;F::armed=true;const int rc=f.core.initializeImportedBaseline(a);F::armed=false;
  assert(rc==C::Uncertain&&!f.core.ready());f.store.finishOperation();}
 assert(root().size()==48&&!memcmp(root().data(),"RTENLI1",8)&&!memcmp(root().data()+12,w.fingerprint,32)&&!counter()&&originals()==original);F::reset();return w;
}
static Bytes object(const A::Witness&w,const Rows&rows=expected(),uint64_t id=2){
 uint32_t mapBytes=0;for(auto&r:rows)mapBytes+=20+r.second.size();Bytes b(24+484+mapBytes);auto*p=b.data()+24;
 memcpy(p,"RTENR03",8);put(p+8,6);put(p+16,1);put(p+96,rows.size());put(p+104,mapBytes);put(p+108,484+mapBytes);
 auto*baseline=p+384;memcpy(baseline,"RTENBP1",8);put(baseline+8,6);put(baseline+12,2);put(baseline+16,mapBytes);put(baseline+20,rows.size());memcpy(baseline+24,w.fingerprint,32);put(baseline+88,1);
 uint32_t at=480;for(auto&r:rows){strcpy(reinterpret_cast<char*>(p+at),r.first.c_str());put(p+at+16,r.second.size());memcpy(p+at+20,r.second.data(),r.second.size());at+=20+r.second.size();}
 Bytes hash(12+mapBytes);put(hash.data(),6);put(hash.data()+4,rows.size());put(hash.data()+8,mapBytes);if(mapBytes)memcpy(hash.data()+12,p+480,mapBytes);SHA256(hash.data(),hash.size(),baseline+56);put(baseline+92,Old::crc(baseline,92));put(p+at,Old::crc(p,at));
 memcpy(b.data(),"RTENOBJ",8);put(b.data()+8,uint32_t(id));put(b.data()+12,uint32_t(id>>32));put(b.data()+16,484+mapBytes);put(b.data()+20,Old::crc(p,484+mapBytes));return b;
}
static void basic(){
 for(bool existing:{false,true}){const auto w=receiptOnly(existing);const auto original=originals();{F::Fixture f;Bundle b(f);assert(f.core.load()==C::Initializing);A::Witness refused;assert(b.sources.inventory(&refused)==C::Conflict&&!refused.namespaceId);assert(initialize(f,b,true,w)==C::Ok&&counter()==1);verify(f,w);assert(originals()==original);}
  F::reset();{F::Fixture f;Bundle b(f);assert(f.core.load()==C::Ok);verify(f,w);C::Initialization a;assert(b.factory.admitImportedResume(&a)==C::Conflict);assert(F::put(f.core,"later","preserved")==C::Ok);f.store.finishOperation();}
  F::reset();{F::Fixture f;Bundle b(f);C::Initialization a;const auto before=F::nvs;assert(b.factory.admitImportedResume(&a)==C::Conflict&&F::nvs==before);assert(f.core.load()==C::Ok&&F::value(f.core,"later")=="preserved");f.store.finishOperation();}}
 puts("Imported init: exact receipt/source binding resumes original absent or empty rnr state; selected root and later edits cannot be reinitialized PASS");
}
static void prefixes(){
 const auto w=receiptOnly();const auto original=originals(),saved=F::nvs;const auto canonical=object(w,expected(),16);
 for(size_t n=0;n<=canonical.size();++n){reset();F::nvs=saved;sequence(8);auto b=canonical;b.resize(n);file(".pending",b);F::Fixture f;Bundle sources(f);assert(initialize(f,sources,true,w)==C::Ok&&counter()==9&&f.store.activeObject()==18);verify(f,w);assert(originals()==original&&!std::filesystem::exists(F::rootPath+"/n00000006/.pending"));}
 reset();F::nvs=saved;sequence(8);file("replica-a.bin",canonical);{F::Fixture f;Bundle sources(f);assert(initialize(f,sources,true,w)==C::Ok&&counter()==9&&f.store.activeObject()==18);verify(f,w);}
 printf("Imported init: %zu exact pending prefixes and full canonical bank re-reserve monotonically; no staged bank promotion PASS\n",canonical.size()+1);
}
static void refusals(){
 const auto w=receiptOnly();const auto saved=F::nvs;const auto canonical=object(w);unsigned cases=0;
 for(unsigned mode=0;mode<36;++mode){reset();F::nvs=saved;sequence(1);uint8_t owner[16];memcpy(owner,Owner,16);const char*legacy=LegacyOwner;
  if(mode==0)F::nvs["rte00000006"]["original"]={9}; // Shadowed source still binds.
  if(mode==1)F::nvs["rte00000006"].erase("untouched");
  if(mode==2)F::nvs["rte00000006"]["foreign"]={1};
  if(mode==3)F::nvs["kvd00000006"]["original"]=delta(3,{5});
  if(mode==4){auto&v=F::nvs["kvj00000006"]["snapshot"];v[24+strlen(LegacyOwner)+1]^=1;repair(v);}
  if(mode==5)F::nvs["kvj00000006"]["state2"]=anchor2();
  if(mode==6)F::nvs["rnr00000006"]["unknown"]={1};
  if(mode==7){auto&v=F::nvs["rnr00000006"]["root"];memcpy(v.data(),"RTENINIT",8);repair(v);}
  if(mode==8){auto&v=F::nvs["rnr00000006"]["root"];memcpy(v.data(),"RTENFI1",8);repair(v);}
  if(mode==9){auto&v=F::nvs["rnr00000006"]["root"];v[12]^=1;repair(v);}
  if(mode==10){auto&v=F::nvs["rnr00000006"]["root"];put(v.data()+8,7);repair(v);}
  if(mode==11)F::nvs["rnr00000006"]["root"].back()^=1;
  if(mode==12)F::nvs["rnr00000006"].erase("root");
  if(mode==13)sequence(0);
  if(mode==14)sequence(UINT64_MAX/2+1);
  if(mode==15){auto&v=F::nvs["rnr00000006"]["next"];put(v.data()+16,1);repair(v);}
  if(mode==16)F::nvs["rnr00000006"]["next"].back()^=1;
  if(mode==17)file("replica-b.bin",canonical);
  if(mode==18)file("unknown",{});
  if(mode==19){auto b=canonical;b.back()^=1;file("replica-a.bin",b);}
  if(mode==20){auto b=canonical;b.pop_back();file("replica-a.bin",b);}
  if(mode==21){auto b=canonical;b.push_back(0);file(".pending",b);}
  if(mode==22)file(".pending",object(w,expected(),4));
  if(mode==23)file(".pending",object(w,expected(),1));
  if(mode==24)file(".pending",object(w,expected(),0));
  if(mode==25){file("replica-a.bin",canonical);std::filesystem::create_symlink("replica-a.bin",F::rootPath+"/n00000006/.pending");}
  if(mode==26){file("replica-a.bin",canonical);std::filesystem::create_hard_link(F::rootPath+"/n00000006/replica-a.bin",F::rootPath+"/n00000006/.pending");}
  if(mode==27){F::nvs["rnr00000006"].erase("next");file(".pending",{});}
  if(mode==28){auto rows=expected();rows["original"]={10};file("replica-a.bin",object(w,rows));}
  if(mode==29){auto different=w;different.fingerprint[1]^=1;file("replica-a.bin",object(different));}
  if(mode==30){owner[0]^=1;}
  if(mode==31)legacy="different-native-owner";
  if(mode==32)sequence(UINT64_MAX/2);
  if(mode==33)F::nvs["kvj00000006"]["snapshot"]=journal({},7,true);
  if(mode==34)F::nvs["kvd00000006"].clear();
  if(mode==35)F::nvs.erase("rte00000006");
  const auto metadata=F::nvs;const auto artifacts=files();{F::Fixture f;Bundle sources(f,owner,legacy);const int rc=initialize(f,sources,true,w);if(rc==C::Ok)fprintf(stderr,"unexpected import refusal success mode=%u\n",mode);assert(rc!=C::Ok&&!f.core.ready()&&F::nvs==metadata&&files()==artifacts);}++cases;
 }
 for(size_t at=0;at<canonical.size();++at){reset();F::nvs=saved;sequence(1);auto b=canonical;b[at]^=1;file("replica-a.bin",b);const auto metadata=F::nvs;{F::Fixture f;Bundle sources(f);assert(initialize(f,sources,true,w)!=C::Ok&&!f.core.ready()&&F::nvs==metadata);}++cases;}
 printf("Imported init: %u root/source/owner/sequence/map/artifact mutations and every full-object byte corruption refuse without overwriting evidence PASS\n",cases);
}
static void callbacks(){
 receiptOnly();const auto saved=F::nvs;unsigned cases=0;
 for(unsigned mode=0;mode<6;++mode){reset();F::nvs=saved;F::Fixture f;Bundle b(f);C::ImportInitReceipt receipt;auto backend=f.store.backend();assert(backend.readImportedInitialization(backend.context,&receipt)==C::Ok);
  struct Mutation {unsigned mode;bool changed=false;Rows rows;};Mutation state{mode,false,{}};
  A::Rows rows{&state,[](void*p,const char*k,const void*v,uint32_t n){auto&s=*static_cast<Mutation*>(p);auto out=sink(s.rows);const int rc=out.append(out.context,k,v,n);if(!s.changed){s.changed=true;if(s.mode==0)F::nvs["rte00000006"]["original"]={19};if(s.mode==1){auto&root=F::nvs["rnr00000006"]["root"];root[12]^=1;repair(root);}if(s.mode==2)sequence(2);if(s.mode==3)F::nvs["rnr00000006"]["unknown"]={1};if(s.mode==4)++epoch;if(s.mode==5)file("unknown",{});}return rc;}};
  A::Witness out;auto api=b.sources.sources();assert(api.resumeImportedMaterialize(api.context,6,receipt,rows,&out)!=C::Ok&&!out.namespaceId);++cases;
 }
 reset();F::nvs=saved;{F::Fixture f;Bundle b(f);b.sourceHash.changeAfterFinish=true;C::Initialization a;assert(b.factory.admitImportedResume(&a)==C::Conflict&&!a.valid());++cases;}
 for(unsigned mode=0;mode<4;++mode){reset();F::nvs=saved;F::Fixture f;Bundle b(f);C::Initialization a;assert(b.factory.admitImportedResume(&a)==C::Ok);if(mode==0)F::nvs["rte00000006"]["original"]={19};if(mode==1){auto&v=F::nvs["rnr00000006"]["root"];v[12]^=1;repair(v);}if(mode==2)file("unknown",{});if(mode==3)++epoch;const auto meta=F::nvs;assert(f.core.initializeImportedBaseline(a)!=C::Ok&&!f.core.ready()&&F::nvs==meta);f.store.finishOperation();++cases;}
 printf("Imported init: %u hash/append/post-admission source, receipt, sequence, inventory, epoch and artifact changes prevent publication PASS\n",cases);
}
static void gatingAndMapIntegrity(){
 const auto w=receiptOnly();const auto saved=F::nvs;unsigned cases=0;
 for(bool resume:{false,true})for(unsigned mode=0;mode<7;++mode){if(!resume&&!mode)continue;reset();if(resume)F::nvs=saved;else seed();F::Fixture f;Bundle bundle(f);auto backend=f.store.backend();auto sources=bundle.sources.sources();
  if(mode==0)sources.importedInventoryVersion=0;
  if(mode==1)sources.importedInventoryVersion=2;
  if(mode==2)sources.resumeImportedMaterialize=nullptr;
  if(mode==3)sources.recheckImported=nullptr;
  if(mode==4)backend.readImportedInitialization=nullptr;
  if(mode==5)backend.beginImportedInitialization=nullptr;
  if(mode==6)backend.endImportedInitialization=nullptr;
  C core(6,backend,S::limits(),C::heap());A factory(&bundle.sources,core,guard(),bundle.mapHash.api(),sources);C::Initialization a;const auto before=F::nvs;
  assert((resume?factory.admitImportedResume(&a):factory.admitImported(selection(w),&a))==C::Invalid&&!a.valid()&&F::nvs==before);++cases;
 }
 for(bool resume:{false,true}){reset();if(resume)F::nvs=saved;else seed();F::Fixture f;Bundle bundle(f);C::Initialization a;assert((resume?bundle.factory.admitImportedResume(&a):bundle.factory.admitImported(selection(w),&a))==C::Ok);assert(a.mapSize()>20);const_cast<uint8_t*>(a.map())[20]^=1;const auto before=F::nvs;assert(f.core.initializeImportedBaseline(a)==C::Conflict&&!f.core.ready()&&F::nvs==before);f.store.finishOperation();++cases;}
 reset();F::nvs=saved;{F::Fixture f;Bundle bundle(f);C::ImportInitReceipt receipt;auto backend=f.store.backend();assert(backend.readImportedInitialization(backend.context,&receipt)==C::Ok);auto api=bundle.sources.sources();const auto before=F::nvs;assert(api.importedInventoryVersion==A::ImportedRecoveryV1&&api.recheckImported(api.context,6,receipt,w)==C::Ok&&F::nvs==before);auto altered=w;++altered.kind;assert(api.recheckImported(api.context,6,receipt,altered)==C::Conflict);altered=w;++altered.revision;assert(api.recheckImported(api.context,6,receipt,altered)==C::Conflict);altered=w;altered.root=A::RootState::InterruptedInit;assert(api.recheckImported(api.context,6,receipt,altered)==C::Conflict);cases+=4;}
 printf("Imported init: %u explicit-version/backend/collector gates, complete recheck bindings and admitted-map tamper refusals PASS\n",cases);
}
static C::Backend unalteredBackend{};static int alteredRootResult=C::Ok;
static void rootRevisionIntegrity(){
 for(bool resume:{false,true}){A::Witness w{};if(resume)w=receiptOnly();else {reset();seed();}
  const auto original=originals();
  {F::Fixture f;Bundle bundle(f);if(!resume)w=inventory(bundle);unalteredBackend=f.store.backend();auto backend=unalteredBackend;alteredRootResult=C::Ok;
   backend.replaceRoot=[](void*p,const void*bytes,uint32_t n){Bytes changed(static_cast<const uint8_t*>(bytes),static_cast<const uint8_t*>(bytes)+n);put(changed.data()+16,2);repair(changed);return alteredRootResult=unalteredBackend.replaceRoot(p,changed.data(),n);};
   C core(6,backend,S::limits(),C::heap());A factory(&bundle.sources,core,guard(),bundle.mapHash.api(),bundle.sources.sources());C::Initialization a;
   assert((resume?factory.admitImportedResume(&a):factory.admitImported(selection(w),&a))==C::Ok);
   assert(core.initializeImportedBaseline(a)==C::Uncertain&&alteredRootResult==C::Invalid&&!core.ready());f.store.finishOperation();
   assert(root().size()==48&&!memcmp(root().data(),"RTENLI1",8)&&!memcmp(root().data()+12,w.fingerprint,32)&&counter()==1&&originals()==original);
  }
  F::reset();{F::Fixture f;Bundle bundle(f);assert(f.core.load()==C::Initializing);assert(initialize(f,bundle,true,w)==C::Ok&&counter()==2);verify(f,w);assert(originals()==original);}
 }
 puts("Imported init: altered root revision refuses before receipt replacement; create/resume reboot retries remain recoverable PASS");
}
struct MapHashCustodyFault {
 Hash hash;unsigned calls=0,cut=0,ioAtFault=0;bool makeRetained=false;
 bool fail(){if(++calls!=cut)return false;retained=makeRetained;F::lease=false;ioAtFault=F::nvsTotal+F::fsTotal;return true;}
 A::Digest api(){return {this,
  [](void*p){auto&s=*static_cast<MapHashCustodyFault*>(p);if(s.fail())return false;const auto h=s.hash.api();return h.begin(h.context);},
  [](void*p,const void*bytes,uint32_t n){auto&s=*static_cast<MapHashCustodyFault*>(p);if(s.fail())return false;const auto h=s.hash.api();return h.update(h.context,bytes,n);},
  [](void*p,uint8_t out[32]){auto&s=*static_cast<MapHashCustodyFault*>(p);if(s.fail())return false;const auto h=s.hash.api();return h.finish(h.context,out);}};}
};
static void mapHashCustody(){
 unsigned cases=0;
 for(bool resume:{false,true})for(bool makeRetained:{false,true})for(unsigned cut=1;;++cut){
  A::Witness w{};if(resume)w=receiptOnly();else {reset();seed();}const auto original=originals();bool reached=false;
  {F::Fixture f;Bundle bundle(f);if(!resume)w=inventory(bundle);MapHashCustodyFault digest;digest.cut=cut;digest.makeRetained=makeRetained;
   A factory(&bundle.sources,f.core,guard(),digest.api(),bundle.sources.sources());C::Initialization a;
   int rc=resume?factory.admitImportedResume(&a):factory.admitImported(selection(w),&a);if(!rc)rc=f.core.initializeImportedBaseline(a);
   reached=digest.calls>=cut;
   if(reached){assert(digest.calls==cut&&!f.core.ready()&&F::nvsTotal+F::fsTotal==digest.ioAtFault);
    if(makeRetained)assert(rc==C::Retained&&!factory.exitSafe()&&!bundle.sources.exitSafe());else assert(rc==C::Busy||rc==C::Uncertain);
    const auto selected=root();assert(selected.empty()||!memcmp(selected.data(),"RTENLI1",8));assert(originals()==original);
   }else {assert(rc==C::Ok);verify(f,w);}
   f.store.finishOperation();
  }
  if(!reached)break;
  ++cases;F::reset();retained=false;
  {F::Fixture f;Bundle bundle(f);const int loaded=f.core.load();assert(loaded==C::Missing||loaded==C::Initializing);assert(initialize(f,bundle,loaded==C::Initializing,w)==C::Ok);verify(f,w);assert(originals()==original);}
 }
 printf("Imported init: %u map-digest false plus Retained/lease-loss callback cuts stop hashing and I/O, preserve receipt and recover after reboot PASS\n",cases);
}
struct Track {
 std::map<void*,size_t> live;unsigned calls=0,fail=0;size_t active=0,peak=0,total=0;
 ~Track(){assert(live.empty()&&!active);}
 C::Allocator api(){return {this,[](void*p,size_t n)->void*{auto&t=*static_cast<Track*>(p);if(++t.calls==t.fail)return nullptr;void*out=malloc(n);assert(out);t.live[out]=n;t.active+=n;t.total+=n;t.peak=std::max(t.peak,t.active);return out;},[](void*p,void*bytes){auto&t=*static_cast<Track*>(p);auto it=t.live.find(bytes);assert(it!=t.live.end());C::wipe(bytes,it->second);t.active-=it->second;t.live.erase(it);free(bytes);}};}
};
static void allocations(){
 receiptOnly();const auto saved=F::nvs;
 for(unsigned failAt=1;failAt<=3;++failAt){reset();F::nvs=saved;F::Fixture f;Allocation allocation;allocation.failAt=failAt;Bundle bundle(f,Owner,LegacyOwner,30000,allocation.api());C::ImportInitReceipt receipt;auto backend=f.store.backend();assert(backend.readImportedInitialization(backend.context,&receipt)==C::Ok);auto api=bundle.sources.sources();Rows rows;A::Witness out;assert(api.resumeImportedMaterialize(api.context,6,receipt,sink(rows),&out)==C::NoMemory&&!out.namespaceId&&rows.empty()&&allocation.live.empty()&&bundle.sources.exitSafe());}
 for(unsigned failAt=1;;++failAt){reset();F::nvs=saved;F::Fixture f;Bundle bundle(f);bundle.sourceHash.failAt=failAt;C::ImportInitReceipt receipt;auto backend=f.store.backend();assert(backend.readImportedInitialization(backend.context,&receipt)==C::Ok);auto api=bundle.sources.sources();Rows rows;A::Witness out;const int rc=api.resumeImportedMaterialize(api.context,6,receipt,sink(rows),&out);if(bundle.sourceHash.calls<failAt){assert(rc==C::Ok);break;}assert(rc==C::Io&&!out.namespaceId&&bundle.sources.exitSafe());}
 for(bool maximum:{false,true})for(bool resume:{false,true}){
  reset();Rows rows;if(maximum){for(unsigned i=0;i<32;++i){char key[16];snprintf(key,sizeof key,"k%02u",i);rows[key]=Bytes(i==31?900:2048,uint8_t(i));}F::nvs["rte00000006"]=rows;F::nvs["rnr00000006"]={};F::nvsCapacity=0x80000;}else {seed();rows=expected();}
  const uint32_t capacity=maximum?65028:30000;A::Witness selected{};
  if(resume){F::Fixture f;Bundle b(f,Owner,LegacyOwner,capacity);selected=inventory(b);C::Initialization a;assert(b.factory.admitImported(selection(selected),&a)==C::Ok);F::fault="nvs-set";F::cut=1;F::nativeCalls=0;F::after=true;F::armed=true;assert(f.core.initializeImportedBaseline(a)==C::Uncertain);F::armed=false;f.store.finishOperation();}
  const auto original=originals();Track track;F::fileAllocation=track.api();
  {F::Fixture f(6,track.api());Bundle b(f,Owner,LegacyOwner,capacity,track.api());if(!resume)selected=inventory(b);assert(initialize(f,b,resume,selected)==C::Ok);verify(f,selected,rows);assert(originals()==original);if(maximum)assert(f.core.baseline().mapSize()==65028);f.store.finishOperation();}
  F::fileAllocation=C::heap();assert(track.live.empty());printf("Imported init allocations (%s %s): %u allocations, %zu requested bytes total, %zu bytes peak; complete original-source rematerialization is repeated at custody checks\n",maximum?"65028-byte map":"84-byte map",resume?"resume":"create",track.calls,track.total,track.peak);
 }
 puts("Imported init: bounded scratch allocation/hash failures unwind and wipe; all shared core/admission/store/source allocations released PASS");
}
static void allocationCuts(){
 unsigned cases=0;const auto w=receiptOnly();const auto saved=F::nvs;
 for(bool resume:{false,true})for(unsigned failAt=1;;++failAt){
  reset();if(resume)F::nvs=saved;else seed();const auto original=originals();Track tracked;tracked.fail=failAt;bool reached=false;
  F::fileAllocation=tracked.api();
  {F::Fixture f(6,tracked.api());Bundle b(f,Owner,LegacyOwner,30000,tracked.api());const int rc=initialize(f,b,resume,w);reached=tracked.calls>=failAt;
   assert(reached?(rc==C::NoMemory||rc==C::Io||rc==C::Uncertain):rc==C::Ok);assert(originals()==original);}
  F::fileAllocation=C::heap();assert(tracked.live.empty());
  {F::Fixture f;Bundle b(f);const int loaded=f.core.load();assert(loaded==C::Ok||loaded==C::Missing||loaded==C::Initializing);
   if(loaded!=C::Ok)assert(initialize(f,b,loaded==C::Initializing,w)==C::Ok);
   verify(f,w);assert(originals()==original);f.store.finishOperation();}
  if(!reached)break;
  ++cases;
 }
 printf("Imported init: %u shared core/admission/store/AppData/source allocation-failure positions recover exact original bytes after reboot PASS\n",cases);
}
static void faults(){
 unsigned cases=0,retainedCases=0,uncertainCases=0;const char*kinds[]={"nvs-open","nvs-count","nvs-find","nvs-probe","nvs-read","nvs-set","nvs-commit","write","partial","read","sync","close","rename-before","rename-after"};
 for(bool resume:{false,true})for(const char*kind:kinds)for(bool after:{false,true})for(unsigned cut=1;;++cut){
  A::Witness w{};if(resume)w=receiptOnly();else {reset();seed();F::Fixture f;Bundle b(f);w=inventory(b);}
  const auto original=originals();bool reached=false;int result=0;
  {F::Fixture f;Bundle b(f);F::fault=kind;F::cut=cut;F::after=after;F::nativeCalls=F::fsCalls=0;F::armed=true;result=initialize(f,b,resume,w);F::armed=false;reached=F::nativeCalls>=cut||F::fsCalls>=cut;
   if(result==C::Retained){++retainedCases;assert(!b.factory.exitSafe()&&(!f.store.exitSafe()||!b.sources.exitSafe()));}
   if(result==C::Uncertain)++uncertainCases;
   assert(originals()==original&&iterators.empty()&&F::handles.empty());if(!reached)assert(result==C::Ok);
  }
  F::reset();{F::Fixture f;Bundle b(f);const int loaded=f.core.load();if(loaded!=C::Ok&&loaded!=C::Missing&&loaded!=C::Initializing)fprintf(stderr,"reload fail kind=%s cut=%u resume=%d after=%d result=%d load=%d\n",kind,cut,resume,after,result,loaded);assert(loaded==C::Ok||loaded==C::Missing||loaded==C::Initializing);
   if(loaded!=C::Ok){const int rc=initialize(f,b,loaded==C::Initializing,w);if(rc!=C::Ok)fprintf(stderr,"recovery fail kind=%s cut=%u resume=%d after=%d result=%d load=%d recovery=%d\n",kind,cut,resume,after,result,loaded,rc);assert(rc==C::Ok);}
   verify(f,w);assert(originals()==original);f.store.finishOperation();}
  if(!reached)break;
  ++cases;
 }
 printf("Imported init: %u reached create/resume NVS and filesystem returned-error cuts (%u Retained, %u Uncertain); reboot loads selected root or resumes exact source receipt PASS\n",cases,retainedCases,uncertainCases);
}
}
#ifndef NATIVE_IMPORT_NO_MAIN
int main(int argc,char**argv){assert(argc==2);setbuf(stdout,nullptr);F::rootPath=argv[1];std::filesystem::create_directories(F::rootPath);ImportRecoveryTest::basic();ImportRecoveryTest::prefixes();ImportRecoveryTest::refusals();ImportRecoveryTest::callbacks();ImportRecoveryTest::gatingAndMapIntegrity();ImportRecoveryTest::rootRevisionIntegrity();ImportRecoveryTest::mapHashCustody();ImportRecoveryTest::allocations();ImportRecoveryTest::allocationCuts();ImportRecoveryTest::faults();reset();puts("Imported initialization remains disconnected; host fault evidence is not ESP filesystem/hardware qualification");}

#endif
