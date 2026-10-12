#include "native_fresh_source_fixture.h"
#include "experimental/native_replica/CheckedNvsFreshSources.h"
#include <set>
namespace F=NativeFixture;
using A=RiscExperimental::NativeBaselineAdmission;
using Sources=RiscExperimental::CheckedNvsFreshSources;
struct Iterator {std::string space;std::vector<std::string> keys;size_t at=0;};
static std::set<Iterator*> iterators;
static bool corruptInfo=false,hideFind=false,retained=false,lateGuard=false,reenter=false,retainWhileOpen=false;
static Sources*active=nullptr;static unsigned reentries=0;
static const uint8_t Owner[16]={'R','i','s','c','R','T','E','/','N','a','t','i','v','e','1',0};
extern "C" esp_err_t nvs_get_used_entry_count(nvs_handle_t h,size_t*n){
 ++F::nvsTotal;F::safe();if(F::hit("nvs-count"))return ESP_FAIL;assert(F::handles.count(h));*n=F::nvs[F::handles.at(h).name].size();return ESP_OK;
}
extern "C" nvs_iterator_t nvs_entry_find(const char*partition,const char*space,nvs_type_t type){
 ++F::nvsTotal;F::safe();assert(!strcmp(partition,"nvs")&&type==NVS_TYPE_ANY);
 if(hideFind||F::hit("nvs-find"))return nullptr;
 auto found=F::nvs.find(space);if(found==F::nvs.end()||found->second.empty())return nullptr;
 auto*i=new Iterator;iterators.insert(i);i->space=space;for(auto&e:found->second)i->keys.push_back(e.first);return i;
}
extern "C" nvs_iterator_t nvs_entry_next(nvs_iterator_t it){
 ++F::nvsTotal;F::safe();auto*i=static_cast<Iterator*>(it);assert(iterators.count(i));if(++i->at<i->keys.size())return i;iterators.erase(i);delete i;return nullptr;
}
extern "C" void nvs_entry_info(nvs_iterator_t it,nvs_entry_info_t*out){
 ++F::nvsTotal;F::safe();auto*i=static_cast<Iterator*>(it);assert(iterators.count(i));*out={};strcpy(out->namespace_name,i->space.c_str());strcpy(out->key,i->keys.at(i->at).c_str());out->type=corruptInfo?NVS_TYPE_U8:NVS_TYPE_BLOB;
}
extern "C" void nvs_release_iterator(nvs_iterator_t it){
 ++F::nvsTotal;F::safe();auto*i=static_cast<Iterator*>(it);if(!i)return;assert(iterators.erase(i));delete i;
}
static Sources::Hooks hooks(){return {nullptr,
 [](void*){if(lateGuard){retained=true;F::lease=false;}return F::onWorker&&F::lease;},
 [](void*){return F::initialized?ESP_OK:ESP_ERR_NVS_NOT_INITIALIZED;},
 [](void*){if(retainWhileOpen&&!F::handles.empty()){retained=true;F::lease=false;}if(reenter&&active){reenter=false;A::Witness out;auto api=active->sources();assert(api.freshInventory(api.context,6,&out)==C::Busy);++reentries;reenter=true;}return retained?int(C::Retained):int(C::Ok);}};}
static A::Guard guard(){return {nullptr,[](void*){return F::onWorker&&F::lease;},[](void*){return uint64_t(19);}};}
static int initialize(F::Fixture&f,Sources&s,bool resume=false){
 A factory(&s,f.core,guard(),{},s.sources(),{});C::Initialization a;
 const int rc=resume?factory.admitFreshResume(&a):factory.admitFreshEmpty(&a);
 return rc?rc:f.core.initializeFreshEmpty(a);
}
static Bytes root(){auto n=F::nvs.find("rnr00000006");return n==F::nvs.end()||!n->second.count("root")?Bytes{}:n->second.at("root");}
static void reset(){corruptInfo=hideFind=retained=lateGuard=reenter=retainWhileOpen=false;active=nullptr;assert(iterators.empty());F::empty();}
static void receiptOnly(){reset();F::Fixture f;Sources s(6,Owner,f.inspector,hooks());F::armed=true;F::fault="nvs-set";F::cut=1;F::after=true;assert(initialize(f,s)==C::Uncertain);F::armed=false;f.store.finishOperation();assert(root().size()==48&&!memcmp(root().data(),"RTENFI1",8));}
static void fresh(){
 reset();F::Fixture f;Sources s(6,Owner,f.inspector,hooks());auto api=s.sources();A::Witness w;const auto before=F::nvs;
 assert(api.freshInventory(api.context,6,&w)==C::Ok&&s.exitSafe()&&F::nvs==before);
 assert(w.namespaceId==6&&w.revision==1&&w.root==A::RootState::NeverInitialized&&!memcmp(w.fingerprint,"RTENFV1",8));
 active=&s;reenter=true;assert(api.recheck(api.context,6,w)==C::Ok&&reentries);reenter=false;active=nullptr;
 assert(initialize(f,s)==C::Ok&&f.core.baseline().origin()==C::Origin::FreshEmpty);
 assert(F::put(f.core,"wifi.synthetic","retained")==C::Ok);f.store.finishOperation();
 assert(api.freshInventory(api.context,6,&w)==C::Conflict);
 puts("NVS source collector: actual checked factory/create path, all legacy namespaces, stable owner witness and callback reentry refusal PASS");
}
static void legacy(){
 unsigned cases=0;
 for(const char*prefix:{"rte","kvj","kvd"})for(const char*key:{"profile","snapshot","state2","unknown"}){
  reset();F::nvs[std::string(prefix)+"00000006"][key]={0,1,2};const auto before=F::nvs;
  F::Fixture f;Sources s(6,Owner,f.inspector,hooks());assert(initialize(f,s)==C::Conflict&&F::nvs==before&&!f.core.ready()&&s.exitSafe());++cases;
 }
 reset();for(const char*prefix:{"rte","kvj","kvd"})F::nvs[std::string(prefix)+"00000006"]={};
 F::nvs["rte00000007"]["valuable"]={7};F::Fixture f;Sources s(6,Owner,f.inspector,hooks());assert(initialize(f,s)==C::Ok&&F::nvs.at("rte00000007").at("valuable")==Bytes{7});f.store.finishOperation();
 printf("NVS source collector: %u competing row/journal/anchor/delta/unknown-key refusals; empty spaces and other namespace isolation PASS\n",cases);
}
static void resumed(){
 receiptOnly();{F::Fixture f;Sources s(6,Owner,f.inspector,hooks());assert(f.core.load()==C::Initializing&&initialize(f,s,true)==C::Ok);f.store.finishOperation();}
 unsigned cases=0;for(unsigned mode=0;mode<8;++mode){receiptOnly();auto before=F::nvs;uint8_t owner[16];memcpy(owner,Owner,16);
  if(mode==0)owner[0]^=1;
  if(mode==1)F::nvs["rnr00000006"]["unknown"]={1};
  if(mode==2)corruptInfo=true;
  if(mode==3)hideFind=true;
  if(mode==4)F::nvs["rnr00000006"]["next"]={1};
  if(mode==5)F::nvs["kvj00000006"]["state2"]={1};
  if(mode==6)F::nvs["rnr00000006"]["root"][0]^=1;
  if(mode==7){std::filesystem::create_directories(F::rootPath+"/n00000006");std::ofstream(F::rootPath+"/n00000006/foreign")<<"preserve";}
  before=F::nvs;F::Fixture f;Sources s(6,owner,f.inspector,hooks());assert(initialize(f,s,true)!=C::Ok&&F::nvs==before&&!f.core.ready()&&s.exitSafe());++cases;
 }
 printf("NVS source collector: checked receipt resume and %u owner/metadata/type/iterator/source/artifact refusals preserve all bytes PASS\n",cases);
}
static void faults(){
 unsigned cases=0;for(bool resume:{false,true})for(const char*kind:{"nvs-open","nvs-count","nvs-find","nvs-probe","nvs-read"})for(bool after:{false,true})for(unsigned cut=1;cut<100;++cut){
  if(resume)receiptOnly();else reset();
  for(const char*prefix:{"rte","kvj","kvd","rnr"})F::nvs[std::string(prefix)+"00000006"];
  if(resume){Bytes next(24);memcpy(next.data(),"RTENSEQ",8);F::FreshNativeFacts::put(next.data()+8,8);F::FreshNativeFacts::put(next.data()+20,F::FreshNativeFacts::crc(next.data(),20));F::nvs["rnr00000006"]["next"]=next;}
  F::Fixture f;Sources s(6,Owner,f.inspector,hooks());auto api=s.sources();A::Witness out;C::FreshInitReceipt receipt;
  if(resume){auto b=f.store.backend();assert(b.readInitialization(b.context,&receipt)==C::Ok);}
  const auto before=F::nvs;F::fault=kind;F::cut=cut;F::after=after;F::nativeCalls=0;F::armed=true;
  const int rc=resume?api.resumeFreshInventory(api.context,6,receipt,&out):api.freshInventory(api.context,6,&out);F::armed=false;
  const bool reached=F::nativeCalls>=cut;assert(F::nvs==before&&s.exitSafe()&&F::handles.empty()&&iterators.empty());
  if(!reached){assert(rc==C::Ok);break;}if(rc==C::Ok)fprintf(stderr,"fault resume=%u kind=%s after=%u cut=%u calls=%u rc=%d\n",resume,kind,after,cut,F::nativeCalls,rc);assert(rc!=C::Ok&&!out.namespaceId);++cases;
 }
 printf("NVS source collector: %u reached read-only NVS failures reject evidence without writes or leaked handles PASS\n",cases);
}
static void prefixes(){
 for(unsigned n=0;n<=508;++n){receiptOnly();Bytes next(24);memcpy(next.data(),"RTENSEQ",8);F::FreshNativeFacts::put(next.data()+8,8);F::FreshNativeFacts::put(next.data()+20,F::FreshNativeFacts::crc(next.data(),20));F::nvs["rnr00000006"]["next"]=next;
  const auto image=F::FreshNativeFacts::canonical();Bytes object(508);memcpy(object.data(),"RTENOBJ",8);F::FreshNativeFacts::put(object.data()+8,16);F::FreshNativeFacts::put(object.data()+16,484);F::FreshNativeFacts::put(object.data()+20,F::FreshNativeFacts::crc(image.data(),image.size()));memcpy(object.data()+24,image.data(),image.size());
  std::filesystem::create_directories(F::rootPath+"/n00000006");{std::ofstream out(F::rootPath+"/n00000006/.pending",std::ios::binary);out.write(reinterpret_cast<const char*>(object.data()),n);assert(out.good());}
  F::Fixture f;Sources s(6,Owner,f.inspector,hooks());assert(initialize(f,s,true)==C::Ok&&f.store.activeObject()==18);f.store.finishOperation();
 }
 puts("NVS source collector: all 509 exact checked scratch prefixes resume through actual factory/store without reusing IDs PASS");
}
static void custody(){
 reset();{F::Fixture f;Sources s(6,Owner,f.inspector,hooks());auto api=s.sources();A::Witness out;F::onWorker=false;auto calls=F::nvsTotal;assert(api.freshInventory(api.context,6,&out)==C::Busy&&F::nvsTotal==calls);F::onWorker=true;F::initialized=false;assert(api.freshInventory(api.context,6,&out)==C::Io&&F::nvsTotal==calls);F::initialized=true;lateGuard=true;assert(api.freshInventory(api.context,6,&out)==C::Retained&&!s.exitSafe()&&F::nvsTotal==calls);}
 F::lease=true;lateGuard=retained=false;reset();
 {F::Fixture f;Sources s(6,Owner,f.inspector,hooks());F::nvs["rte00000006"]={};retainWhileOpen=true;auto api=s.sources();A::Witness out;assert(api.freshInventory(api.context,6,&out)==C::Retained&&!s.exitSafe()&&!F::handles.empty());const auto calls=F::nvsTotal;assert(api.freshInventory(api.context,6,&out)==C::Retained&&F::nvsTotal==calls);}
 // Simulated process reset abandons retained native handles without closing them.
 F::handles.clear();F::lease=true;retainWhileOpen=retained=false;reset();
 puts("NVS source collector: owner-worker/initialization refusal and late guard Retained precedence with zero NVS I/O PASS");
}
int main(int argc,char**argv){assert(argc==2);F::rootPath=argv[1];setbuf(stdout,nullptr);fresh();legacy();resumed();prefixes();faults();custody();reset();printf("Collector host size=%zu; no collector-owned heap allocation, no production selection, ESP artifact inspector remains unsupported\n",sizeof(Sources));}
