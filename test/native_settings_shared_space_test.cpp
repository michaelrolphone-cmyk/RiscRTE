// Real backend, captured mount and pinned LittleFS; host NVS/worker boundary.
// No peer file is deleted to obtain capacity. This is not live product admission.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define heap_caps_malloc original_fixture_heap_caps_malloc
#define main original_backend_main
#include "native_settings_backend_test.cpp"
#undef main
#undef heap_caps_malloc
#pragma GCC diagnostic pop
#include <fstream>
#include <filesystem>
#include <cstdlib>
static unsigned payloadCalls=0,refuseAt=0;
void*heap_caps_malloc(size_t n,unsigned flags){
 ++payloadCalls;assert(flags==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 return payloadCalls==refuseAt?nullptr:original_fixture_heap_caps_malloc(n,flags);
}
static Bytes flashImage(){Bytes b(native_lfs_fixture_flash_size());assert(native_lfs_fixture_snapshot(b.data(),b.size())==b.size());return b;}
static bool local=false,cutSeal=false,stageRefusal=false;
static unsigned peerCount=0,expectedPass=0;
static uint64_t expectedRevision=0,expectedSequence=0;
static std::string snapshot;
static void saveState(){
 std::filesystem::create_directories(snapshot);
 auto b=flashImage();std::ofstream flash(snapshot+"/flash.bin",std::ios::binary);flash.write(reinterpret_cast<const char*>(b.data()),b.size());flash.close();assert(flash);
 std::ofstream rows(snapshot+"/nvs.bin",std::ios::binary);
 auto word=[&](uint32_t n){rows.write(reinterpret_cast<const char*>(&n),4);};
 auto bytes=[&](const auto&v){word(v.size());rows.write(reinterpret_cast<const char*>(v.data()),v.size());};
 word(nvs.size());for(const auto&space:nvs){bytes(space.first);word(space.second.size());for(const auto&row:space.second){bytes(row.first);bytes(row.second);}}
 rows.close();assert(rows);std::ofstream expected(snapshot+"/expected.txt");expected<<peerCount<<" "<<expectedPass<<" "<<expectedRevision<<" "<<expectedSequence<<"\n";expected.close();assert(expected);
}
static void loadState(){
 std::ifstream flash(snapshot+"/flash.bin",std::ios::binary);Bytes b{std::istreambuf_iterator<char>(flash),{}};assert(b.size()==native_lfs_fixture_flash_size()&&!native_lfs_fixture_restore(b.data(),b.size()));
 std::ifstream rows(snapshot+"/nvs.bin",std::ios::binary);
 auto word=[&](){uint32_t n=0;rows.read(reinterpret_cast<char*>(&n),4);assert(rows&&n<1024*1024);return n;};
 auto str=[&](){auto n=word();assert(n<64);std::string v(n,' ');rows.read(v.data(),n);assert(rows);return v;};
 nvs.clear();auto count=word();assert(count<32);for(unsigned i=0;i<count;++i){auto name=str();auto entries=word();assert(entries<128);for(unsigned j=0;j<entries;++j){auto key=str();auto n=word();auto&v=nvs[name][key];v.resize(n);rows.read(reinterpret_cast<char*>(v.data()),n);assert(rows);}}
 std::ifstream expected(snapshot+"/expected.txt");expected>>peerCount>>expectedPass>>expectedRevision>>expectedSequence;assert(expected&&peerCount<128&&expectedPass<100);
}
static Bytes peerBytes(unsigned i){return Bytes(4096,uint8_t(17+i));}
static std::string peerPath(unsigned i){return "/shared-space/peer-"+std::to_string(i);}
static void readExact(const std::string&path,const Bytes&expected){
 const int fd=native_lfs_fixture_open(path.c_str(),O_RDONLY);assert(fd>=0);Bytes got(expected.size());
 assert(native_lfs_fixture_read(fd,got.data(),got.size())==ptrdiff_t(got.size())&&got==expected);char extra;assert(native_lfs_fixture_read(fd,&extra,1)==0&&!native_lfs_fixture_close(fd));
}
static void verifyPeers(){
 liveWorker=false;onOwner=true;M::Guard g(M::Access::AppData);assert(g.held);
 for(unsigned ns=1;ns<=4;++ns)readExact("/n0000000"+std::to_string(ns)+"/peer",Bytes(64,uint8_t(ns)));
 for(unsigned i=0;i<peerCount;++i)readExact(peerPath(i),peerBytes(i));
}
static void addPeersUntil(uint64_t targetFree,bool near){
 liveWorker=false;onOwner=true;M::Guard g(M::Access::AppData);assert(g.held);
 size_t total=0,used=0;
 for(;;){
  assert(M::usage(&total,&used)&&total==F::Store::PartitionBytes&&used<=total);
  const auto free=total-used;if(near?free<=targetFree+8192:free<targetFree)break;
  assert(peerCount<128);auto b=peerBytes(peerCount);assert(!native_lfs_fixture_seed(peerPath(peerCount).c_str(),b.data(),b.size()));++peerCount;
 }
 printf("PEERS files=%u total=%zu used=%zu free=%zu target=%llu; no removal\n",peerCount,total,used,total-used,static_cast<unsigned long long>(targetFree));
}
struct Audit {
 C::Backend original{};
 uint64_t required=0,free=0;uint32_t bytes=0;unsigned calls=0,exclusions=0;
} audit;
static void excludedPeer(){
 bool admitted=true;
 std::thread other([&](){M::Guard g(M::Access::AppData);admitted=g.held;});other.join();
 assert(!admitted&&!M::retained());++audit.exclusions;
}
static int reserveChecked(void*p,uint32_t n,uint64_t*out){
 assert(M::held()&&liveWorker);++audit.calls;audit.bytes=n;
 excludedPeer();size_t total=0,used=0;assert(M::usage(&total,&used)&&total==F::Store::PartitionBytes&&used<=total);
 audit.free=total-used;audit.required=((uint64_t(n)+F::Store::ObjectHeader+4095)/4096)*4096+F::Store::RecoveryReserve;
 if(stageRefusal)refuseAt=payloadCalls+1;
 const int rc=audit.original.reserve(p,n,out);refuseAt=0;
 excludedPeer();
 printf("RESERVE image=%u free=%llu required=%llu recovery=%u result=%d\n",n,static_cast<unsigned long long>(audit.free),static_cast<unsigned long long>(audit.required),F::Store::RecoveryReserve,rc);
 if(audit.free<audit.required)assert(rc==C::NoSpace&&!*out);
 if(stageRefusal)assert(audit.free>=audit.required&&rc==C::NoMemory&&!*out);
 return rc;
}
static int sealChecked(void*p,uint64_t id){
 const int rc=audit.original.seal(p,id);
 if(cutSeal&&rc==C::Ok){
  assert(M::held()&&liveWorker);saveState();
  puts("PROCESS CUT after actual immutable bank seal, before selected root publication; prior root/legacy/peers retained");fflush(stdout);std::_Exit(77);
 }
 return rc;
}
static void verifyPayload(C&core,unsigned pass){
 verifyValue(core,"original","kept");
 for(unsigned i=0;i<4;++i){const auto key="opaque_"+std::to_string(i);Bytes got(512);uint32_t n=0;assert(core.get(key.c_str(),got.data(),got.size(),&n)==C::Ok&&n==got.size()&&got==Bytes(512,uint8_t(pass+i)));}
}
static int edit(unsigned pass,int expected,bool cut=false){
 const auto&b=inputs.wifi.backend;active(6);M::Guard g(M::Access::Settings);assert(g.held);
 const auto policy=local?C::DurabilityPolicy::LocalOnlyV1:C::DurabilityPolicy::ExternalVolume;
 F::Store store(6,*b.files,b.metadata,b.guard,RiscCpu::NativeSettingsPayloadAllocator::allocator(),b.artifacts,policy);
 audit.original=store.backend();auto hooked=audit.original;hooked.reserve=reserveChecked;hooked.seal=sealChecked;
 C core(6,hooked,F::Store::limits(),RiscCpu::NativeSettingsPayloadAllocator::allocator(),policy);assert(core.load()==C::Ok);store.finishOperation();
 const auto revision=core.revision(),sequence=core.sequence();const auto before=nvs;const auto flash=flashImage();const auto allocationsBefore=payloadCalls;const auto liveBefore=blocks.size();
 std::string keys[4];Bytes values[4];C::Mutation changes[4]{};
 for(unsigned i=0;i<4;++i){keys[i]="opaque_"+std::to_string(i);values[i]=Bytes(512,uint8_t(pass+i));changes[i]={keys[i].c_str(),C::Put,values[i].data(),uint32_t(values[i].size())};}
 const std::string intent="shared-space-"+std::to_string(pass);
 expectedRevision=revision;expectedSequence=sequence;cutSeal=cut;const auto reserveCalls=audit.calls;
 const int rc=core.apply(revision,changes,4,intent.data(),intent.size());cutSeal=false;
 assert(rc==expected&&audit.calls==reserveCalls+1);
 if(rc==C::Ok){expectedPass=pass;expectedRevision=revision+1;expectedSequence=sequence+1;assert(core.revision()==expectedRevision&&core.sequence()==expectedSequence);verifyPayload(core,pass);}
 else{
  // These exact failures occur before physical allocation/publication. This
  // equality is intentionally not asserted after the sealed-bank cut.
  assert(core.revision()==revision&&core.sequence()==sequence&&nvs==before&&flashImage()==flash);
  if(stageRefusal)assert(payloadCalls>allocationsBefore+1&&blocks.size()==liveBefore);
 }
 store.finishOperation();assert(core.load()==C::Ok);store.finishOperation();
 if(expectedPass)verifyPayload(core,expectedPass);
 assert(core.revision()==expectedRevision&&core.sequence()==expectedSequence&&store.exitSafe()&&!M::retained()&&handles.empty()&&iterators.empty());
 if(local)assert(core.localProofCount()==1&&!core.pendingCount()&&core.baseline().archiveObligation());
 else assert(core.pendingCount()&&core.baseline().archiveObligation());
 return rc;
}
static void legacyPreserved(){
 assert(nvs.at("rte00000001").at("legacy_peer")==Bytes({'k','e','e','p'}));
 assert(nvs.at("rte00000006").at("original")==Bytes({'k','e','p','t'}));
 if(!local)assert(nvs.at("rte00000001").at("original")==Bytes({'k','e','p','t'}));
}
static void reloadSelected(){
 const auto before=nvs;active(6);M::Guard g(M::Access::Settings);assert(g.held);const auto&b=inputs.wifi.backend;
 const auto policy=local?C::DurabilityPolicy::LocalOnlyV1:C::DurabilityPolicy::ExternalVolume;
 F::Store store(6,*b.files,b.metadata,b.guard,RiscCpu::NativeSettingsPayloadAllocator::allocator(),b.artifacts,policy);
 C core(6,store.backend(),F::Store::limits(),RiscCpu::NativeSettingsPayloadAllocator::allocator(),policy);
 assert(core.load()==C::Ok&&core.revision()==expectedRevision&&core.sequence()==expectedSequence);verifyPayload(core,expectedPass);store.finishOperation();
 assert(core.baseline().archiveObligation()&&nvs==before&&store.exitSafe()&&!M::retained());
 if(local)assert(core.formatVersion()==5&&!core.pendingCount()&&core.localProofCount()==1);
 else assert(core.formatVersion()==3&&core.pendingCount());
}
int main(int argc,char**argv){
 assert(argc==2);const std::string mode=argv[1];local=mode.rfind("local",0)==0;
 const bool reboot=mode.find("reboot")!=std::string::npos,cut=mode.find("cut")!=std::string::npos;
 assert(mode==(local?"local":"external")||mode==(local?"local-reboot":"external-reboot")||mode==(local?"local-cut":"external-cut")||mode==(local?"local-cut-reboot":"external-cut-reboot"));
 const char*base=getenv("SHARED_SPACE_OUTPUT");assert(base&&*base);snapshot=std::string(base)+"/"+(local?"local":"external")+(cut?"-cut":"");
 assert(!native_lfs_fixture_reset());if(reboot)loadState();ready();
 if(reboot){reloadSelected();legacyPreserved();verifyPeers();assert(blocks.empty());puts("PASS separate-process real LittleFS reload: prior selected root, opaque payload, legacy and peer files preserved");return 0;}
 {M::Guard g(M::Access::AppData);assert(g.held);for(unsigned ns=1;ns<=4;++ns){const auto path="/n0000000"+std::to_string(ns);assert(!native_lfs_fixture_mkdir(path.c_str()));Bytes b(64,uint8_t(ns));assert(!native_lfs_fixture_seed((path+"/peer").c_str(),b.data(),b.size()));}assert(!native_lfs_fixture_mkdir("/shared-space"));}
 nvs["rte00000001"]["legacy_peer"]={'k','e','e','p'};
 if(!local)exercise(0,"legacy");exercise(1,local?"local-legacy":"legacy");
 const auto legacyOne=nvs.at("rte00000001"),legacySix=nvs.at("rte00000006");
 edit(1,C::Ok);verifyPeers();
 stageRefusal=true;edit(2,C::NoMemory);stageRefusal=false;verifyPeers();
 addPeersUntil(audit.required+8192,true);verifyPeers();
 if(cut){legacyPreserved();edit(2,C::Ok,true);assert(false);}
 edit(2,C::Ok);verifyPeers();
 addPeersUntil(F::Store::RecoveryReserve,false);verifyPeers();
 edit(3,C::NoSpace);verifyPeers();
 assert(nvs.at("rte00000001")==legacyOne&&nvs.at("rte00000006")==legacySix);
 legacyPreserved();assert(audit.exclusions==2*audit.calls&&blocks.empty()&&!M::retained());saveState();
 puts("PASS shared-space admission/peer growth/refusal; no peer deletion, no durable reservation or live heap claim");return 0;
}
