// Actual X4/FatFs composition. Reuse the exact native custody gate callbacks,
// task roles and copied provider worker from the lower-level attempt fixture.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main native_sd_attempt_fixture_main
#include "native_sd_attempt_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include "native_sd_consumers_fixture.h"
#include "experimental/native_replica/CheckedNativeExportRecovery.h"
#include "experimental/native_replica/CheckedLegacySettingsReader.h"
#include "experimental/native_replica/CheckedSdSettingsCatalog.h"
#include <openssl/evp.h>
#include <map>
#include <string>
#include <vector>
using T=RiscExperimental::CheckedSdReplicaCheckpoint;
using R=RiscExperimental::CheckedSdReplicaReader;
using L=RiscExperimental::CheckedLegacySettingsReader;
using K=RiscExperimental::CheckedSdSettingsCatalog;
using E=RiscExperimental::CheckedNativeExportRecovery;
using Bytes=std::vector<uint8_t>;
extern "C" bool consumers_observe_allowed(){return task==Task::Owner;}
static unsigned nativeOps,snapshots,snapshotFaultAt;
static uint64_t sourceGeneration=7;
static bool snapshotUnavailable,snapshotRetained;
static A::Hooks controlledHooks(){auto h=hooks();h.snapshot=[](void*p,uint64_t*out){if(++snapshots==snapshotFaultAt){if(snapshotRetained){fixture_retain();lease=false;}else if(snapshotUnavailable)fixture_unavailable();else ++epoch;}return hooks().snapshot(p,out);};return h;}
struct NativeStore {
 Bytes root;std::map<uint64_t,Bytes> objects;uint64_t next=1;unsigned busyReserves=0;bool retainSeal=false,blockCommit=false;
 static void check(){assert(task==Task::NativeSettings&&!fixture_raw_io_allowed());++nativeOps;}
 C::Backend api(){return {this,
  [](void*p,void*out,uint32_t n){check();auto&s=*static_cast<NativeStore*>(p);if(s.root.empty())return int(C::Missing);assert(s.root.size()==n);memcpy(out,s.root.data(),n);return int(C::Ok);},
  [](void*p,uint32_t n,uint64_t*out){check();auto&s=*static_cast<NativeStore*>(p);if(s.busyReserves){--s.busyReserves;return int(C::Busy);}*out=s.next++;s.objects[*out]=Bytes(n);return int(C::Ok);},
  [](void*p,uint64_t id,uint32_t at,const void*in,uint32_t n){check();auto&b=static_cast<NativeStore*>(p)->objects.at(id);assert(at+n<=b.size());memcpy(b.data()+at,in,n);return int(C::Ok);},
  [](void*p,uint64_t id,uint32_t at,void*out,uint32_t n){check();auto&b=static_cast<NativeStore*>(p)->objects.at(id);assert(at+n<=b.size());memcpy(out,b.data()+at,n);return int(C::Ok);},
  [](void*p,uint64_t){check();auto&s=*static_cast<NativeStore*>(p);if(s.blockCommit){std::unique_lock<std::mutex>lock(gate);enteredNative=true;cv.notify_all();cv.wait(lock,[]{return allowNative;});}return int(s.retainSeal?C::Retained:C::Ok);},
  [](void*p,const void*in,uint32_t n){check();auto&s=*static_cast<NativeStore*>(p);s.root.assign(static_cast<const uint8_t*>(in),static_cast<const uint8_t*>(in)+n);return int(C::Ok);},
  [](void*p,uint64_t id){check();static_cast<NativeStore*>(p)->objects.erase(id);}};}
};
struct Hash {
 EVP_MD_CTX*ctx=EVP_MD_CTX_new();bool fail=false;
 ~Hash(){EVP_MD_CTX_free(ctx);}
 T::Digest api(){return {this,
  [](void*p){auto&s=*static_cast<Hash*>(p);return !s.fail&&EVP_DigestInit_ex(s.ctx,EVP_sha256(),nullptr)==1;},
  [](void*p,const void*b,uint32_t n){assert(n<=4096);auto&s=*static_cast<Hash*>(p);return !s.fail&&EVP_DigestUpdate(s.ctx,b,n)==1;},
  [](void*p,uint8_t*out){auto&s=*static_cast<Hash*>(p);unsigned n=0;return !s.fail&&EVP_DigestFinal_ex(s.ctx,out,&n)==1&&n==32;}};}
};
static T::Guard guard(){return {nullptr,[](void*){return lease;},[](void*){return task==Task::NativeSettings;}};}
static bool quietRetain;
static E::Quiescence quiet(){return {nullptr,[](void*){assert(task==Task::Owner);if(quietRetain){fixture_retain();lease=false;return false;}return true;}};}
static void put32(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(i*8));}
static uint32_t crc(const uint8_t*p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
static Bytes baseMap(){Bytes b(52);memcpy(b.data(),"base",4);put32(b.data()+16,32);memset(b.data()+20,'v',32);return b;}
static C::Identity mapIdentity(const Bytes&b,uint32_t count,uint64_t generation=7){Bytes data(12);put32(data.data(),3);put32(data.data()+4,count);put32(data.data()+8,b.size());data.insert(data.end(),b.begin(),b.end());C::Identity id{};id.known=true;id.generation=generation;id.lineage[0]=17;unsigned n=0;assert(EVP_Digest(data.data(),data.size(),id.digest,&n,EVP_sha256(),nullptr)==1&&n==32);return id;}
static std::string page(const C::Identity&id){char p[160],digest[65];for(unsigned i=0;i<32;++i)snprintf(digest+2*i,3,"%02x",id.digest[i]);snprintf(p,sizeof p,"/RTECFG/00000003/nr-%08x%08x-%s-00000000",unsigned(id.generation>>32),unsigned(id.generation),digest);return p;}
static void seedReplica(bool corrupt=false,bool missing=false){const Bytes b=baseMap();auto id=mapIdentity(b,1,sourceGeneration);uint8_t index[T::IndexBytes]{};T::makeIndex(index,3,b.size(),1,id);if(corrupt)index[83]^=1;if(!missing)consumers_seed_file("/RTECFG/00000003/replica.index",index,sizeof index);consumers_seed_file(page(id).c_str(),b.data(),b.size());}
static void seedLegacy(){for(unsigned single=0;single<2;++single){Bytes b(single?212:184);memcpy(b.data(),single?"RISCKV1":"RISCKV2",8);put32(b.data()+8,3);put32(b.data()+16,7);strcpy(reinterpret_cast<char*>(b.data()+24),"shared-00000003");put32(b.data()+120,1);if(!single)put32(b.data()+124,b.size());unsigned at=single?124:128;strcpy(reinterpret_cast<char*>(b.data()+at),"legacy");put32(b.data()+at+16,32);memset(b.data()+at+20,'v',32);put32(b.data()+b.size()-4,crc(b.data(),b.size()-4));if(single)consumers_seed_file("/RTECFG/00000003/settings.kv",b.data(),b.size());else{Bytes index(28);memcpy(index.data(),"RTEKVP2",8);put32(index.data()+8,b.size());put32(index.data()+12,7);put32(index.data()+20,crc(b.data(),b.size()));put32(index.data()+24,crc(index.data(),24));consumers_seed_file("/RTECFG/00000003/settings.index",index.data(),index.size());consumers_seed_file("/RTECFG/00000003/kv-0000000000000007-00000000",b.data(),b.size());}}}
static void initialize(C&core){task=Task::NativeSettings;C::Value v{"base","vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv",32};assert(core.initialize(&v,1,mapIdentity(baseMap(),1,sourceGeneration))==C::Ok);task=Task::Owner;}
static void edit(C&core,const char*value){task=Task::NativeSettings;C::Mutation m{"edited",C::Put,value,uint32_t(strlen(value))};assert(core.apply(core.revision(),&m,1,"synthetic-intent",16)==C::Ok);task=Task::Owner;}
static std::string value(C&core){char b[64];uint32_t n=0;assert(core.get("edited",b,sizeof b,&n)==C::Ok);return std::string(b,n);}
struct Counters {unsigned reads=fixture_reads(),writes=fixture_writes(),observations=consumers_observations(),submissions=consumers_submissions();void unchanged()const{assert(reads==fixture_reads()&&writes==fixture_writes()&&observations==consumers_observations()&&submissions==consumers_submissions());}};
template<class Job>static int prepare(Job&job,A&a,bool cancelAfter=false){
 assert(a.receiptPinned()&&a.release()==A::Context);Counters before;
 assert(!job.preparationWorkerStep());task=Task::ProviderIo;assert(!job.preparationWorkerStep());task=Task::Owner;
 job.step();before.unchanged();assert(job.collectPreparation()==C::Busy);
 task=Task::NativeSettings;assert(job.preparationWorkerStep());before.unchanged();assert(job.collectPreparation()!=C::Ok);
 task=Task::Owner;job.step();before.unchanged();if(cancelAfter)job.cancel();const int rc=job.collectPreparation();assert(!a.receiptPinned()||rc==C::Retained);return rc;
}
static void strictRaw(C&core,const risc_storage_volume_api_v1*v){Hash hash;R raw(core,hash.api(),guard());task=Task::NativeSettings;assert(raw.start(v)==C::Ok);task=Task::Owner;const unsigned count=consumers_submissions();raw.step();raw.step();assert(raw.state()==R::State::Failed&&raw.exitSafe()&&consumers_submissions()==count);}
template<class Job>static void drive(Job&job,A&a,C&core,const risc_storage_volume_api_v1*v,bool probe=false){unsigned rounds=0;while(job.state()==Job::State::Running){
 Counters before;task=Task::NativeSettings;job.step();before.unchanged();task=Task::Other;job.step();before.unchanged();task=Task::Owner;
 const unsigned nativeBefore=nativeOps;job.step();assert(nativeOps==nativeBefore);
 if(probe&&a.hasSession()){assert(fixture_observe()==RISC_STORAGE_STATE_UNAVAILABLE);strictRaw(core,v);probe=false;}
 if(!worker.idle()){step();}assert(++rounds<2000);
 }assert(consumers_refreshes()==1);if(job.state()==Job::State::Verified){Hash competingHash;R competing(core,competingHash.api(),guard());assert(competing.prepareNative(a)==C::Busy&&competing.exitSafe());}}
static void cleanup(A&a){assert(a.release()==C::Ok&&a.exitSafe()&&!fenceHeld);attempt=nullptr;fixture_stop();}
template<class Job>static int pollingComplete(Job&job,A&a,NativeStore&native,bool busy){
 std::atomic<bool>stop{false};std::atomic<unsigned>polls{0},executions{0};if(busy)native.busyReserves=2;
 const unsigned reads=fixture_reads(),writes=fixture_writes(),submits=consumers_submissions();
 std::thread thread([&]{task=Task::NativeSettings;while(!stop.load()){if(job.nativeCompletionWorkerStep())++executions;++polls;std::this_thread::yield();}});
 while(polls.load()<100){std::this_thread::yield();}assert(job.issueNativeCompletion()==C::Ok&&a.release()==A::Context);
 int rc=C::Busy;for(unsigned n=0;rc==C::Busy&&n<1000000;++n){rc=job.collectNativeCompletion();std::this_thread::yield();}assert(rc==C::Ok);
 const unsigned after=polls.load();while(polls.load()<after+100){std::this_thread::yield();}stop.store(true);thread.join();
 assert(executions.load()==(busy?3u:1u)&&reads==fixture_reads()&&writes==fixture_writes()&&submits==consumers_submissions());return rc;
}
template<class Job>static int complete(Job&job,A&a,NativeStore&native,const char*mode){
 assert(a.release()==A::Context&&!job.exitSafe());if(strstr(mode,"poll"))return pollingComplete(job,a,native,strstr(mode,"busy"));task=Task::NativeSettings;assert(job.issueNativeCompletion()!=C::Ok);task=Task::Owner;const unsigned ops=nativeOps;assert(job.issueNativeCompletion()==C::Ok&&a.receiptPinned());assert(a.release()==A::Context);assert(!job.nativeCompletionWorkerStep());task=Task::ProviderIo;assert(!job.nativeCompletionWorkerStep());task=Task::Owner;assert(job.collectNativeCompletion()==C::Busy);Counters before;
 if(strstr(mode,"running-cancel")){native.blockCommit=true;std::thread t([&]{task=Task::NativeSettings;assert(job.nativeCompletionWorkerStep());});{std::unique_lock<std::mutex>lock(gate);cv.wait(lock,[]{return enteredNative;});}job.cancel();assert(a.release()==A::Context&&!job.exitSafe());{std::lock_guard<std::mutex>lock(gate);allowNative=true;}cv.notify_all();t.join();native.blockCommit=false;}
 else {if(strstr(mode,"cancel"))job.cancel();if(strstr(mode,"retained")&&!strstr(mode,"late-retained")&&!strstr(mode,"final-release"))native.retainSeal=true;task=Task::NativeSettings;assert(job.nativeCompletionWorkerStep());task=Task::Owner;}
 before.unchanged();task=Task::NativeSettings;assert(job.collectNativeCompletion()!=C::Ok);job.cancel();task=Task::Owner;assert(a.release()==A::Context);if(strstr(mode,"late-retained")){fixture_retain();lease=false;}if(strstr(mode,"completion-unavailable")){snapshotFaultAt=snapshots+1;snapshotUnavailable=true;}if(strstr(mode,"final-release")){snapshotFaultAt=snapshots+2;snapshotRetained=strstr(mode,"retained");}if(strstr(mode,"cancel")&&!strstr(mode,"running-cancel"))assert(nativeOps==ops);return job.collectNativeCompletion();
}
int main(int argc,char**argv){assert(argc==3);const char*mode=argv[1];sourceGeneration=strstr(mode,"zero")?0:7;task=Task::Owner;const auto*v=fixture_start();if(!strncmp(mode,"seed-",5)){consumers_seed_begin();seedReplica(strstr(mode,"corrupt"),strstr(mode,"missing"));seedLegacy();consumers_seed_save(argv[2]);fixture_stop();return 0;}consumers_seed_load(argv[2]);consumers_trace_enable();A a({3,55,7,71,v},controlledHooks());attempt=&a;bootstrap(a,v);assert(consumers_refreshes()==1);NativeStore native;C core(3,native.api(),{65512,4096,1024},C::heap());initialize(core);Hash hash;
 if(!strncmp(mode,"reader",6)){
  C::Allocator allocation=C::heap();if(strstr(mode,"no-memory"))allocation.allocate=[](void*,size_t)->void*{return nullptr;};
  R job(core,hash.api(),guard(),allocation);task=Task::Other;assert(job.prepareNative(a)!=C::Ok);task=Task::Owner;assert(job.prepareNative(a)==C::Ok);
  if(strstr(mode,"cancel-prep")){job.cancel();}if(strstr(mode,"prep-retained"))retainOnSnapshot=true;const int prepared=prepare(job,a,strstr(mode,"cancel-after-prep"));if(strstr(mode,"prep-retained")){assert(prepared==C::Retained&&!job.exitSafe()&&!a.exitSafe());return 0;}if(strstr(mode,"no-memory")){assert(prepared==C::NoMemory&&job.exitSafe());cleanup(a);return 0;}if(strstr(mode,"cancel-prep")||strstr(mode,"cancel-after-prep")){assert(prepared==C::Stale&&job.exitSafe());cleanup(a);return 0;}assert(prepared==C::Ok);
  if(strstr(mode,"malformed-busy")){consumers_malformed_busy();}if(strstr(mode,"-io")){consumers_fail_next_result();}if(!strcmp(mode,"reader-cancel"))job.cancel();const unsigned ops=nativeOps;drive(job,a,core,v,true);assert(nativeOps==ops);
  if(strstr(mode,"malformed-busy")){assert(job.state()==R::State::Retained&&!job.exitSafe()&&!a.exitSafe());return 0;}
  if(strstr(mode,"corrupt")||strstr(mode,"missing")){assert(job.state()==R::State::Failed&&job.exitSafe());assert(consumers_recoveries()==(strstr(mode,"missing")?1u:0u));cleanup(a);return 0;}
  if(strstr(mode,"-io")){assert(job.state()==R::State::Failed&&job.error()==C::Io&&job.exitSafe()&&!consumers_recoveries());cleanup(a);return 0;}
  if(!strcmp(mode,"reader-cancel")){assert(job.state()==R::State::Cancelled&&job.exitSafe());cleanup(a);return 0;}
  assert(job.state()==R::State::Verified&&!job.exitSafe()&&a.release()==A::Context);R::Candidate candidate;
  task=Task::NativeSettings;assert(job.take(&candidate)==C::Busy&&!candidate.valid());task=Task::Owner;
  if(strstr(mode,"retained")){fixture_retain();lease=false;assert(job.take(&candidate)==C::Retained&&!candidate.valid()&&!job.exitSafe()&&!a.exitSafe());return 0;}
  if(strstr(mode,"final-")){snapshotFaultAt=snapshots+2;snapshotUnavailable=strstr(mode,"unavailable");const int rc=job.take(&candidate);assert(rc==(snapshotUnavailable?C::Io:C::Conflict)&&!candidate.valid()&&job.exitSafe());if(snapshotUnavailable)cleanup(a);else assert(!a.exitSafe());return 0;}
  assert(job.take(&candidate)==C::Ok&&candidate.valid()&&candidate.namespaceId()==3&&candidate.count()==1&&candidate.mapSize()==baseMap().size()&&!memcmp(candidate.map(),baseMap().data(),baseMap().size())&&job.exitSafe());cleanup(a);
 }else if(!strncmp(mode,"legacy",6)){
  L job(core,hash.api(),guard());assert(job.prepareNative(a,strstr(mode,"single")?L::Source::SingleFile:L::Source::PagedV2)==C::Ok);assert(prepare(job,a)==C::Ok);if(strstr(mode,"-io"))consumers_fail_next_result();drive(job,a,core,v,true);
  if(strstr(mode,"-io")){assert(job.state()==L::State::Failed&&job.error()==C::Io&&job.exitSafe()&&!consumers_recoveries());cleanup(a);return 0;}
  assert(job.state()==L::State::Verified&&!job.exitSafe()&&a.release()==A::Context);L::Candidate candidate;assert(job.take(&candidate)==C::Ok&&candidate.valid()&&candidate.namespaceId()==3&&candidate.count()==1&&candidate.generation()==7&&job.exitSafe());cleanup(a);
 }else if(!strncmp(mode,"catalog",7)){
  K job(core,hash.api(),guard());assert(job.prepareNative(a,strstr(mode,"bounded")?K::Limits{1,128}:K::Limits{})==C::Ok&&prepare(job,a)==C::Ok);if(strstr(mode,"-io"))consumers_fail_next_result();drive(job,a,core,v,true);
  if(strstr(mode,"-io")){assert(job.state()==K::State::Failed&&job.error()==C::Io&&job.exitSafe());cleanup(a);return 0;}
  assert(job.state()==K::State::Verified&&!job.exitSafe()&&a.release()==A::Context);assert(consumers_begins()==6&&a.closedSequence()==6);K::Report report;
  if(strstr(mode,"retained")){fixture_retain();lease=false;assert(job.take(&report)==C::Retained&&!report.valid()&&!job.exitSafe()&&!a.exitSafe());return 0;}
  if(strstr(mode,"final-unavailable")){snapshotFaultAt=snapshots+2;snapshotUnavailable=true;assert(job.take(&report)==C::Io&&!report.valid()&&job.exitSafe());cleanup(a);return 0;}
  assert(job.take(&report)==C::Ok&&report.valid()&&!report.vacant());if(strstr(mode,"bounded"))assert(!report.stableInventory()&&report.initialDirectory()==K::Directory::Incomplete);else assert(report.stableInventory());for(unsigned i=0;i<3;++i)assert(report.slot(i)->content==K::Content::Valid&&report.slot(i)->count==1);assert(job.exitSafe());cleanup(a);
 }else if(!strncmp(mode,"writer",6)){
  edit(core,"captured");uint8_t incarnation[16]={42};
  if(strstr(mode,"namespace")){NativeStore wrongStore;C wrong(4,wrongStore.api(),{65512,4096,1024},C::heap());initialize(wrong);C::Ticket ticket;task=Task::NativeSettings;assert(wrong.capture(&ticket)==C::Ok);task=Task::Owner;T wrongJob(hash.api(),guard());assert(wrongJob.prepareNative(wrong,a)==C::Ok&&prepare(wrongJob,a)==C::Conflict&&wrongJob.exitSafe());task=Task::NativeSettings;assert(wrong.acknowledge(ticket,mapIdentity(baseMap(),1,8))==C::Ok);task=Task::Owner;cleanup(a);return 0;}
  if(strstr(mode,"pending-export")){T old(hash.api(),guard());assert(old.prepareNative(core,a,true,incarnation)==C::Ok&&prepare(old,a)==C::Ok);old.cancel();old.step();assert(old.exitSafe()&&core.exportPrepared());}
  T job(hash.api(),guard());assert(job.prepareNative(core,a,strstr(mode,"durable"),incarnation)==C::Ok);if(strstr(mode,"hash-failure"))hash.fail=true;if(strstr(mode,"cancel-prep"))job.cancel();if(strstr(mode,"prep-retained"))retainOnSnapshot=true;const int prepared=prepare(job,a,strstr(mode,"cancel-after-prep"));if(strstr(mode,"prep-retained")){assert(prepared==C::Retained&&!job.exitSafe()&&!a.exitSafe());return 0;}if(strstr(mode,"pending-export")){assert(prepared==C::Busy&&job.exitSafe()&&core.exportPrepared());cleanup(a);return 0;}if(strstr(mode,"hash-failure")||strstr(mode,"cancel-prep")||strstr(mode,"cancel-after-prep")){assert(prepared!=C::Ok&&job.exitSafe());cleanup(a);return 0;}assert(prepared==C::Ok);if(strstr(mode,"snapshot"))edit(core,"newer");if(strstr(mode,"existing-page"))consumers_fail_page_read();else if(strstr(mode,"-io"))consumers_fail_next_result();drive(job,a,core,v,true);if(strstr(mode,"-io")){assert(job.state()==T::State::Failed&&job.error()==C::Io&&job.exitSafe()&&consumers_recoveries()==(strstr(mode,"existing-page")?1u:0u));if(strstr(mode,"existing-page"))assert(consumers_page_read_faults()==1&&!consumers_mutations());cleanup(a);return 0;}assert(job.state()==T::State::Verified&&core.pendingCount()==(strstr(mode,"snapshot")?2u:1u)&&job.acknowledgeNative()==C::Invalid);if(!strstr(mode,"snapshot"))edit(core,"newer");const int rc=complete(job,a,native,mode);
  if(strstr(mode,"final-release-epoch")){assert(rc==C::Conflict&&job.exitSafe()&&!a.exitSafe());return 0;}if(strstr(mode,"retained")||strstr(mode,"completion-unavailable")){assert(rc==C::Retained&&!job.exitSafe()&&!a.exitSafe()&&a.state()==A::State::Retained);return 0;}
  if(strstr(mode,"cancel")&&!strstr(mode,"running-cancel")){assert(rc==C::Stale&&core.pendingCount()==2);}else assert(rc==C::Ok&&core.pendingCount()==1&&core.identity().generation==8);assert(value(core)=="newer"&&job.exitSafe());cleanup(a);
 }else if(!strncmp(mode,"recovery",8)){
  if(strstr(mode,"no-marker")){E noMarker(core,hash.api(),guard(),quiet());assert(noMarker.prepareNative(a)==C::Ok&&prepare(noMarker,a)==C::Missing&&noMarker.exitSafe());cleanup(a);return 0;}
  edit(core,"captured");uint8_t incarnation[16]={42};{T writer(hash.api(),guard());assert(writer.prepareNative(core,a,true,incarnation)==C::Ok&&prepare(writer,a)==C::Ok);if(strstr(mode,"retire")){writer.cancel();drive(writer,a,core,v);}else{drive(writer,a,core,v);assert(writer.state()==T::State::Verified);writer.cancel();}assert(writer.exitSafe()&&core.exportPrepared());}
  edit(core,"newer");E job(core,hash.api(),guard(),quiet());if(strstr(mode,"quiet-retained-queue")){quietRetain=true;assert(job.prepareNative(a)==C::Retained&&!job.exitSafe()&&!a.exitSafe());return 0;}assert(job.prepareNative(a)==C::Ok);if(strstr(mode,"quiet-retained-collect"))quietRetain=true;if(strstr(mode,"cancel-prep"))job.cancel();const int prepared=prepare(job,a,strstr(mode,"cancel-after-prep"));if(strstr(mode,"quiet-retained-collect")){assert(prepared==C::Retained&&!job.exitSafe()&&!a.exitSafe());return 0;}if(strstr(mode,"cancel-prep")||strstr(mode,"cancel-after-prep")){assert(prepared==C::Stale&&job.exitSafe()&&core.exportPrepared());cleanup(a);return 0;}assert(prepared==C::Ok);if(strstr(mode,"quiet-retained-sd"))quietRetain=true;if(strstr(mode,"-io"))consumers_fail_next_result();drive(job,a,core,v,true);if(strstr(mode,"quiet-retained-sd")){assert(job.state()==E::State::Retained&&!job.exitSafe()&&!a.exitSafe());return 0;}if(strstr(mode,"-io")){assert(job.state()==E::State::Failed&&job.error()==C::Io&&job.exitSafe());cleanup(a);return 0;}assert(job.state()==E::State::Verified&&job.completeNative()==C::Invalid);assert(job.decision()==(strstr(mode,"retire")?E::Decision::RetireUnpublished:E::Decision::AcknowledgePrefix));if(strstr(mode,"quiet-retained-issue")){quietRetain=true;assert(job.issueNativeCompletion()==C::Retained&&!job.exitSafe()&&!a.exitSafe());return 0;}const int rc=complete(job,a,native,mode);if(strstr(mode,"final-release-epoch")){assert(rc==C::Conflict&&job.exitSafe()&&!a.exitSafe());return 0;}if(strstr(mode,"retained")||strstr(mode,"completion-unavailable")){assert(rc==C::Retained&&!job.exitSafe()&&!a.exitSafe());return 0;}if(strstr(mode,"cancel")&&!strstr(mode,"running-cancel")){assert(rc==C::Stale&&core.pendingCount()==2&&core.exportPrepared());}else assert(rc==C::Ok&&core.pendingCount()==(strstr(mode,"retire")?2u:1u)&&!core.exportPrepared());assert(value(core)=="newer"&&job.exitSafe());cleanup(a);
 }else assert(false);
 printf("typed native SD consumer %s PASS (refreshes=%u, child sessions=%u, native operations=%u)\n",mode,consumers_refreshes(),consumers_begins()-1,nativeOps);
}
