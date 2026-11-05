// Actual checked store, NVS collectors and AppDataFiles with process-death
// cuts. Host NVS/VFS proof only: not Watch flash, capacity or target admission.
#define RISC_NATIVE_IMPORT_POWER_CUT_NO_MAIN
#include "native_import_power_cut_test.cpp"
#include "experimental/native_replica/CheckedNvsFreshSources.h"
using Fresh=RiscExperimental::CheckedNvsFreshSources;
static Fresh::Hooks freshHooks(){return {nullptr,[](void*){return F::onWorker&&F::lease;},[](void*){return F::initialized?ESP_OK:ESP_ERR_NVS_NOT_INITIALIZED;},[](void*){return int(C::Ok);}};}
static int finishFresh(PolicyFixture&f,bool resume){
 Fresh sources(6,Owner,f.inspector,freshHooks(),testPolicy);A factory(&sources,f.core,guard(),{},sources.sources());C::Initialization a;
 int rc=resume?factory.admitFreshResume(&a):factory.admitFreshEmpty(&a);
 return rc?rc:f.core.initializeFreshEmpty(a);
}
static void freshCuts(){
 unsigned reached=0;
 for(bool receipt:{false,true})for(unsigned cut=1;cut<200;++cut){
  reset();
  F::nvs["rte00000007"]["peer"]={4,5,6};const auto peer=F::nvs.at("rte00000007");
  if(receipt){PolicyFixture f;F::fault="nvs-set";F::cut=2;F::armed=true;F::after=false;assert(finishFresh(f,false)!=C::Ok);F::armed=false;f.store.finishOperation();assert(!memcmp(F::nvs.at("rnr00000006").at("root").data(),"RTENFL1",8));}
  powerCut=cut;powerAt=0;const pid_t pid=::fork();assert(pid>=0);
  if(!pid){F::powerCheckpoint=powerPoint;PolicyFixture child;assert(finishFresh(child,receipt)==C::Ok);persistNvs();::_exit(0);}
  int status=0;assert(::waitpid(pid,&status,0)==pid&&WIFEXITED(status));const int result=WEXITSTATUS(status);assert(result==0||result==77);
  restoreNvs();F::reset();assert(F::nvs.at("rte00000007")==peer);
  {PolicyFixture reboot;const int rc=reboot.core.load();if(rc==C::Missing||rc==C::Initializing)assert(finishFresh(reboot,rc==C::Initializing)==C::Ok);else assert(rc==C::Ok);
   assert(reboot.core.formatVersion()==5&&reboot.core.revision()==1&&reboot.core.sequence()==0&&reboot.core.baseline().origin()==C::Origin::FreshEmpty);reboot.store.finishOperation();}
  {PolicyFixture twice;assert(twice.core.load()==C::Ok&&twice.core.revision()==1);twice.store.finishOperation();}
  if(result==0)break;
  assert(cut<199);++reached;
 }
 printf("LocalOnly fresh/resume process-death cuts=%u PASS\n",reached);
}
static std::map<std::string,Bytes> fileBytes(){std::map<std::string,Bytes> result;for(auto&e:std::filesystem::recursive_directory_iterator(F::rootPath))if(e.is_regular_file()){std::ifstream in(e.path(),std::ios::binary);result[e.path().string()]=Bytes(std::istreambuf_iterator<char>(in),{});}return result;}
static void crossPolicyRefusals(){
 for(bool imported:{false,true})for(bool receipt:{false,true})for(bool sourceLocal:{false,true}){
  reset();if(imported)seed();testPolicy=sourceLocal?C::DurabilityPolicy::LocalOnlyV1:C::DurabilityPolicy::ExternalVolume;
  {PolicyFixture f;if(receipt){F::fault="nvs-set";F::cut=2;F::armed=true;F::after=false;}A::Witness witness{};int rc=imported?finish(f,false,&witness):finishFresh(f,false);F::armed=false;assert(receipt?rc!=C::Ok:rc==C::Ok);f.store.finishOperation();}
  const auto before=F::nvs;const auto files=fileBytes();
  testPolicy=sourceLocal?C::DurabilityPolicy::ExternalVolume:C::DurabilityPolicy::LocalOnlyV1;
  {PolicyFixture f;assert(f.core.load()!=C::Ok);A::Witness witness{};assert((imported?finish(f,receipt,&witness):finishFresh(f,receipt))!=C::Ok);f.store.finishOperation();}
  assert(F::nvs==before&&fileBytes()==files);
 }
 testPolicy=C::DurabilityPolicy::LocalOnlyV1;
}
static void noUnclassifiedFallback(){
 reset();const auto before=F::nvs;const auto files=fileBytes();
 {PolicyFixture f;auto b=f.store.backend();uint64_t object=99;
  assert(b.reserve(b.context,1024,&object)==C::Conflict&&object==0);
  f.store.finishOperation();}
 assert(F::nvs==before&&fileBytes()==files);
}
static void checkpointCuts(){
 unsigned reached=0;
 for(unsigned cut=1;cut<200;++cut){
  reset();seed();F::nvs["rte00000007"]["peer"]={9};const auto originals=F::nvs;
  A::Witness w{};{PolicyFixture f;assert(finish(f,false,&w)==C::Ok);f.store.finishOperation();}
  powerCut=cut;powerAt=0;const pid_t pid=::fork();assert(pid>=0);
  if(!pid){PolicyFixture child;assert(child.core.load()==C::Ok);F::powerCheckpoint=powerPoint;C::Mutation m{"original",C::Put,"new",3};assert(child.core.apply(child.core.revision(),&m,1,"save",4)==C::Ok);child.store.finishOperation();persistNvs();::_exit(0);}
  int status=0;assert(::waitpid(pid,&status,0)==pid&&WIFEXITED(status));int result=WEXITSTATUS(status);assert(result==0||result==77);restoreNvs();F::reset();assertOriginal(originals);assert(F::nvs.at("rte00000007")==originals.at("rte00000007"));
  {PolicyFixture reboot;assert(reboot.core.load()==C::Ok);std::string value;assert(F::get(reboot.core,"original",&value)==C::Ok);const bool changed=value=="new";assert(changed||value==std::string(1,char(5)));
   assert(reboot.core.revision()==(changed?2u:1u)&&reboot.core.sequence()==(changed?1u:0u)&&reboot.core.pendingCount()==0);
   const auto b=reboot.core.baseline();assert(b.archiveObligation()&&!memcmp(b.sourceFingerprint(),w.fingerprint,32));std::string opaque;assert(F::get(reboot.core,"untouched",&opaque)==C::Ok&&opaque==std::string(1,char(2)));reboot.store.finishOperation();}
  if(result==0)break;
  assert(cut<199);++reached;
 }
 printf("LocalOnly checked checkpoint process-death cuts=%u PASS\n",reached);
}
int main(int argc,char**argv){
 testPolicy=C::DurabilityPolicy::LocalOnlyV1;assert(runImportCuts(argc,argv)==0);
 freshCuts();crossPolicyRefusals();noUnclassifiedFallback();checkpointCuts();std::filesystem::remove(snapshotPath);reset();
 puts("LocalOnly checked fresh/import/receipt/checkpoint recovery PASS; target and global capacity unqualified");return 0;
}
