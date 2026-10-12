// Source-bound actual X4/FatFs checked reader, shared coordinator, checked NVS
// inventory and checked AppData store. No compatibility initializer is called.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main lower_attempt_main
#include "native_sd_attempt_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include "native_sd_consumers_fixture.h"
#define NATIVE_FIXTURE_THREADED 1
#define N CheckedMetadata
#include "native_replica_checked_fixture.h"
#undef N
#include "experimental/native_replica/CheckedSdNativeRestore.h"
#include "experimental/native_replica/NativeSettingsCoordinator.h"
#include <openssl/evp.h>
#include <set>
using Restore=RiscExperimental::CheckedSdNativeRestore;
using Reader=RiscExperimental::CheckedSdReplicaReader;
using Catalog=RiscExperimental::CheckedSdSettingsCatalog;
using Sources=RiscExperimental::CheckedNvsFreshSources;
using Coordinator=RiscExperimental::NativeSettingsCoordinator;
namespace F=NativeFixture;
struct Iterator {std::string space;std::vector<std::string>keys;size_t at=0;};
static std::set<Iterator*>iterators;
extern "C" esp_err_t nvs_get_used_entry_count(nvs_handle_t h,size_t*n){++F::nvsTotal;F::safe();assert(F::handles.count(h));*n=F::nvs[F::handles.at(h).name].size();return ESP_OK;}
extern "C" nvs_iterator_t nvs_entry_find(const char*partition,const char*space,nvs_type_t type){++F::nvsTotal;F::safe();assert(!strcmp(partition,"nvs")&&type==NVS_TYPE_ANY);auto found=F::nvs.find(space);if(found==F::nvs.end()||found->second.empty())return nullptr;auto*i=new Iterator;iterators.insert(i);i->space=space;for(auto&e:found->second)i->keys.push_back(e.first);return i;}
extern "C" nvs_iterator_t nvs_entry_next(nvs_iterator_t it){++F::nvsTotal;F::safe();auto*i=static_cast<Iterator*>(it);assert(iterators.count(i));if(++i->at<i->keys.size())return i;iterators.erase(i);delete i;return nullptr;}
extern "C" void nvs_entry_info(nvs_iterator_t it,nvs_entry_info_t*out){++F::nvsTotal;F::safe();auto*i=static_cast<Iterator*>(it);assert(iterators.count(i));*out={};strcpy(out->namespace_name,i->space.c_str());strcpy(out->key,i->keys.at(i->at).c_str());out->type=NVS_TYPE_BLOB;}
extern "C" void nvs_release_iterator(nvs_iterator_t it){++F::nvsTotal;F::safe();auto*i=static_cast<Iterator*>(it);if(i){assert(iterators.erase(i));delete i;}}
extern "C" bool consumers_observe_allowed(){return task==Task::Owner;}
struct Hash {
 EVP_MD_CTX*ctx=EVP_MD_CTX_new();~Hash(){EVP_MD_CTX_free(ctx);}
 Reader::Digest reader(){return {this,[](void*p){return EVP_DigestInit_ex(static_cast<Hash*>(p)->ctx,EVP_sha256(),nullptr)==1;},[](void*p,const void*b,uint32_t n){return EVP_DigestUpdate(static_cast<Hash*>(p)->ctx,b,n)==1;},[](void*p,uint8_t*out){unsigned n=0;return EVP_DigestFinal_ex(static_cast<Hash*>(p)->ctx,out,&n)==1&&n==32;}};}
 Restore::Digest native(){auto d=reader();return {d.context,d.begin,d.update,d.finish};}
};
static void put(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(8*i));}
static Bytes map(){Bytes b;for(unsigned i=0;i<24;++i){const auto at=b.size();b.resize(at+276);char name[16];snprintf(name,sizeof name,"w%08x.a0",23-i);strcpy(reinterpret_cast<char*>(b.data()+at),name);put(b.data()+at+16,256);for(unsigned j=0;j<256;++j)b[at+20+j]=uint8_t(i+j);}return b;}
static C::Identity identity(const Bytes&map){Bytes input(12);put(input.data(),3);put(input.data()+4,24);put(input.data()+8,map.size());input.insert(input.end(),map.begin(),map.end());C::Identity id{};id.known=true;id.generation=129;id.lineage[0]=42;unsigned n=0;assert(EVP_Digest(input.data(),input.size(),id.digest,&n,EVP_sha256(),nullptr)==1&&n==32);return id;}
static void seed(const char*mode){
 const auto b=map();const auto id=identity(b);uint8_t index[RiscExperimental::CheckedSdReplicaCheckpoint::IndexBytes]{};RiscExperimental::CheckedSdReplicaCheckpoint::makeIndex(index,3,b.size(),24,id);consumers_seed_file("/RTECFG/00000003/replica.index",index,sizeof index);
 char digest[65];for(unsigned i=0;i<32;++i)snprintf(digest+2*i,3,"%02x",id.digest[i]);
 // 130 faithfully formatted immutable generations: 261 actual FatFs entries,
 // well beyond the former 128-entry and 8192-name-byte restore ceilings.
 for(unsigned generation=0;generation<=id.generation;++generation)for(unsigned at=0;at<b.size();at+=4096){char path[160];snprintf(path,sizeof path,"/RTECFG/00000003/nr-%016x-%s-%08x",generation,digest,at/4096);consumers_seed_file(path,b.data()+at,std::min<size_t>(4096,b.size()-at));}
 if(strcmp(mode,"seed")){
  auto next=id;++next.generation;uint8_t artifact[RiscExperimental::CheckedSdReplicaCheckpoint::IndexBytes];RiscExperimental::CheckedSdReplicaCheckpoint::makeIndex(artifact,3,b.size(),24,next);
  if(!strcmp(mode,"seed-stage")||!strcmp(mode,"seed-backup-only")||!strcmp(mode,"seed-foreign-stage")){if(!strcmp(mode,"seed-foreign-stage"))artifact[32]^=1;consumers_seed_file("/RTECFG/00000003/.replica.next",artifact,sizeof artifact);}
  if(!strcmp(mode,"seed-empty-stage"))consumers_seed_file("/RTECFG/00000003/.replica.next",artifact,0);
  if(!strcmp(mode,"seed-backup-only"))consumers_seed_rename("/RTECFG/00000003/replica.index","/RTECFG/00000003/replica.index.risc-replace");
  if(!strcmp(mode,"seed-published-backup")){auto prior=id;--prior.generation;RiscExperimental::CheckedSdReplicaCheckpoint::makeIndex(artifact,3,b.size(),24,prior);consumers_seed_file("/RTECFG/00000003/replica.index.risc-replace",artifact,sizeof artifact);}
  if(strstr(mode,"v2")){
   using Stage=RiscExperimental::SdReplicaStage;char staged[Stage::PathBytes];auto target=next;
   if(!strcmp(mode,"seed-v2-old"))target.generation=1;
   assert(Stage::makePath(staged,sizeof staged,3,target));auto encoded=target;if(!strcmp(mode,"seed-v2-foreign"))encoded.lineage[0]^=1;
   RiscExperimental::CheckedSdReplicaCheckpoint::makeIndex(artifact,3,b.size(),24,encoded);consumers_seed_file(staged,artifact,!strcmp(mode,"seed-v2-empty")?0:sizeof artifact);
   if(!strcmp(mode,"seed-v2-replaced")){
    for(unsigned at=0;at<b.size();at+=4096){char path[160];snprintf(path,sizeof path,"/RTECFG/00000003/nr-%016x-%s-%08x",130,digest,at/4096);consumers_seed_file(path,b.data()+at,std::min<size_t>(4096,b.size()-at));}
    return;
   }
  }
  // Uncommitted target pages, including the actual empty-file adapter case.
  char path[160];snprintf(path,sizeof path,"/RTECFG/00000003/nr-%016x-%s-%08x",130,digest,0);consumers_seed_file(path,b.data(),123);
  snprintf(path,sizeof path,"/RTECFG/00000003/nr-%016x-%s-%08x",130,digest,1);consumers_seed_file(path,b.data(),0);
 }
}

struct Client {
 Coordinator&service;Coordinator::Lease hold;uint32_t slot=0;Catalog reader;Restore restore;
 Client(Coordinator&s,F::Fixture&f,Sources&sources,Hash&readHash,Hash&nativeHash):service(s),reader(f.core,readHash.reader(),{this,[](void*p){auto&s=*static_cast<Client*>(p);return s.service.nativeOwner(s.hold);},[](void*p){auto&s=*static_cast<Client*>(p);return s.service.nativeWorker(s.slot);}}),restore(f.core,f.store,sources,nativeHash.native(),{this,[](void*){return task==Task::Owner;},[](void*){return task==Task::NativeSettings;},[](void*p){auto&s=*static_cast<Client*>(p);return s.service.nativeWorker(s.slot);},[](void*p){return static_cast<Client*>(p)->hold.serial;}}){}
 Coordinator::NativeClient registration(){return {this,sizeof(*this),[](void*p){auto&s=*static_cast<Client*>(p);return s.reader.preparationWorkerStep()||s.restore.workerStep();},[](void*p){auto&s=*static_cast<Client*>(p);return s.reader.state()==Catalog::State::Running||s.restore.pending();},[](void*p){auto&s=*static_cast<Client*>(p);return s.reader.exitSafe()&&s.restore.exitSafe();},[](void*p){auto&s=*static_cast<Client*>(p);return s.reader.state()==Catalog::State::Retained||s.restore.retained();}};}
};
int main(int argc,char**argv){
 assert(argc==4);const char*mode=argv[1];task=Task::Owner;const auto*v=fixture_start();
 if(!strncmp(mode,"seed",4)){consumers_seed_begin();seed(mode);consumers_seed_save(argv[2]);fixture_stop();return 0;}
 consumers_seed_load(argv[2]);consumers_trace_enable();F::rootPath=argv[3];F::empty();F::Fixture f(3);F::onWorker=false;F::lease=false;
 unsigned ended=0;Coordinator service(f.core,f.store,{&ended,[](void*){return task==Task::Owner;},[](void*){return task==Task::NativeSettings;},[](void*){assert(task==Task::Owner);return !F::lease.exchange(true);},[](void*p){assert(task==Task::Owner&&F::lease.exchange(false));++*static_cast<unsigned*>(p);}});
 const uint8_t owner[16]={'R','i','s','c','R','T','E','/','N','a','t','i','v','e','1',0};Sources sources(3,owner,f.inspector,{&f,[](void*){return task==Task::NativeSettings&&F::onWorker&&F::lease;},[](void*){return ESP_OK;},[](void*p){return static_cast<F::Fixture*>(p)->files.retained()?int(C::Retained):int(C::Ok);}});Hash readHash,nativeHash;Client client(service,f,sources,readHash,nativeHash);assert(!service.registerNative(client.registration(),&client.slot)&&!service.activate());
 A a({3,55,7,71,v},hooks());attempt=&a;bootstrap(a,v);
 if(!strcmp(mode,"new-generation")){
  auto target=identity(map());++target.generation;risc_storage_package_request_v1 q{};q.operation=RISC_STORAGE_PACKAGE_REPLACE;assert(RiscExperimental::SdReplicaStage::makePath(q.path,sizeof q.path,3,target));strcpy(q.destination,"/RTECFG/00000003/replica.index");assert(!a.openChild()&&!a.package(q));risc_storage_async_result_v1 result{};int status=RISC_STORAGE_ASYNC_PENDING;while(status==RISC_STORAGE_ASYNC_PENDING){step();status=a.collect(&result);}assert(!status);close(a);
 }
 const unsigned admittedWrites=fixture_writes(),admittedMutations=consumers_mutations();assert(!service.acquireNative(client.slot,&client.hold));
 std::atomic<bool>stop{false};std::atomic<unsigned>polls{0},executions{0};std::thread native([&]{task=Task::NativeSettings;F::onWorker=true;while(!stop.load()){if(service.workerStep())++executions;++polls;std::this_thread::yield();}});
 assert(!client.reader.prepareRestoreNative(a));int rc=C::Busy;while(rc==C::Busy){rc=client.reader.collectPreparation();std::this_thread::yield();}assert(!rc);
 while(client.reader.state()==Catalog::State::Running){const auto calls=F::nvsTotal+F::fsTotal;client.reader.step();assert(F::nvsTotal+F::fsTotal==calls);if(!worker.idle())step();}
 Reader::Candidate candidate;
 if(!strcmp(mode,"catalog-conflict")){assert(client.reader.state()==Catalog::State::Verified&&client.reader.takeRestoreCandidate(&candidate)==C::Conflict&&!candidate.valid()&&!consumers_recoveries()&&consumers_mutations()==admittedMutations&&fixture_writes()==admittedWrites&&F::nvs.empty());stop.store(true);native.join();assert(!service.releaseNative(client.hold));assert(!a.release());attempt=nullptr;fixture_stop();puts("actual X4/FatFs foreign interrupted stage refuses without writes PASS");return 0;}
 assert(client.reader.state()==Catalog::State::Verified&&!client.reader.takeRestoreCandidate(&candidate));
 assert(candidate.count()==24&&candidate.mapSize()==map().size()&&candidate.identity().generation==(!strcmp(mode,"new-generation")?130u:129u)&&!consumers_recoveries()&&consumers_mutations()==admittedMutations&&fixture_writes()==admittedWrites);
 if(!strcmp(mode,"conflict"))F::nvs["rte00000003"]["valuable"]={1,2,3};
 if(!strcmp(mode,"native-retained")){F::fault="close";F::cut=1;F::armed=true;}
 const unsigned reads=fixture_reads(),writes=fixture_writes(),observations=consumers_observations(),submits=consumers_submissions();
 if(!strcmp(mode,"cancel")){stop.store(true);native.join();assert(!client.restore.begin(a,candidate));client.restore.cancel();task=Task::NativeSettings;F::onWorker=true;assert(service.workerStep());F::onWorker=false;task=Task::Owner;}
 else {
  if(!strcmp(mode,"running-cancel")){F::powerCheckpoint=[](const char*at){if(strcmp(at,"write-before"))return;std::unique_lock<std::mutex>lock(gate);enteredNative=true;cv.notify_all();cv.wait(lock,[]{return allowNative;});};}
  assert(!client.restore.begin(a,candidate));
  if(!strcmp(mode,"running-cancel")){std::unique_lock<std::mutex>lock(gate);cv.wait(lock,[]{return enteredNative;});client.restore.cancel();assert(service.releaseNative(client.hold)==C::Busy);allowNative=true;cv.notify_all();}
  if(!strcmp(mode,"late-retained")){while(executions.load()<2)std::this_thread::yield();fixture_retain();}
  rc=C::Busy;while(rc==C::Busy){N::Result result{};const int got=client.restore.collect(&result);rc=got?got:result.status;std::this_thread::yield();}
 }
 N::Result result{};if(!strcmp(mode,"cancel")){rc=C::Busy;while(rc==C::Busy){const int got=client.restore.collect(&result);rc=got?got:result.status;std::this_thread::yield();}}
 if(native.joinable()){stop.store(true);native.join();}F::armed=false;F::powerCheckpoint=nullptr;
 assert(reads==fixture_reads()&&writes==fixture_writes()&&submits==consumers_submissions());assert(consumers_observations()>=observations);
 if(strstr(mode,"retained")){assert(rc==C::Retained&&service.releaseNative(client.hold)==C::Retained&&service.retained()&&!service.exitSafe()&&!a.exitSafe()&&!ended);}
 else {assert(rc==(!strcmp(mode,"cancel")?C::Stale:!strcmp(mode,"conflict")?C::Conflict:C::Ok));if(rc==C::Ok){C::Snapshot snap;assert(f.core.captureSnapshot(&snap)==0&&snap.mapSize()==map().size()&&!memcmp(snap.map(),map().data(),map().size()));}assert(!service.releaseNative(client.hold)&&ended==1&&service.exitSafe());assert(!a.release());attempt=nullptr;fixture_stop();}
 printf("actual X4/FatFs checked native SD restore, 130-generation baseline plus checked artifacts, %s PASS\n",mode);
}
