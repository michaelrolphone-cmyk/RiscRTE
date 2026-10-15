#include "bootstrap/Runtime.h"
#include <RiscPreferenceStoreV1.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <unistd.h>
using R=RiscBoot::Runtime;
static std::string directory,scenario;
static std::thread::id ownerThread=std::this_thread::get_id();
static std::map<std::pair<uint32_t,std::string>,std::string> values;
static R* runtime;
static unsigned writes=0,failWrite=0;static int failStatus=RISC_KEY_VALUE_IO;
static char secondInput[]="second";
static risc_preference_store_v1 saved{};
static risc_bound_key_value_v1 bound{};
static bool owner(){return std::this_thread::get_id()==ownerThread;}
static bool health(risc_runtime_health_v1*){return true;}
static bool logLine(const char* line){fprintf(stderr,"%s\n",line);return true;}
static void delay(uint32_t){}
static int32_t get(void*,uint32_t ns,const char* key,void* out,uint32_t capacity,uint32_t* size){
 *size=0;auto it=values.find({ns,key});if(it==values.end())return RISC_KEY_VALUE_NOT_FOUND;
 if(scenario=="bad-read"){*size=1;if(capacity)memset(out,'z',capacity);return RISC_KEY_VALUE_IO;}
 if(capacity<it->second.size()){*size=it->second.size();return RISC_KEY_VALUE_BUFFER_SMALL;}
 memcpy(out,it->second.data(),it->second.size());*size=it->second.size();return 0;
}
static int32_t put(void*,uint32_t ns,const char* key,const void* data,uint32_t size){
 ++writes;
 if(scenario=="copy" && writes==1)memset(secondInput,'x',sizeof(secondInput)-1);
 if(writes==failWrite)return failStatus;
 values[{ns,key}]=std::string(static_cast<const char*>(data),size);return 0;
}
static const RiscBoot::KeyValueBackend backend{nullptr,get,put,2048};
static risc_preference_store_state_v1 state(const risc_preference_store_v1& a){risc_preference_store_state_v1 s{sizeof(s)};assert(!a.state(a.context,&s));assert(s.flags==4&&s.max_mutations==16&&s.max_value_bytes==2048&&s.revision);return s;}
static void invalid(const risc_preference_store_v1& a){
 risc_preference_store_state_v1 s{sizeof(s)};risc_preference_store_receipt_v1 out{sizeof(out)};uint32_t size=42;uint64_t token=42;char data='q';
 assert(a.state(a.context,&s)==RISC_ATOMIC_INVALID);
 assert(a.get(a.context,1,"sample",&data,1,&size)==RISC_ATOMIC_INVALID&&size==0&&data=='q');
 assert(a.submit(a.context,1,nullptr,0,"save",4,&token)==RISC_ATOMIC_INVALID&&token==0);
 assert(a.collect(a.context,1,&out)==RISC_ATOMIC_INVALID&&a.cancel(a.context,1)==RISC_ATOMIC_INVALID);
}
static risc_runtime_capability_v1 acquire(const risc_runtime_api_v1* rt,const char* cap,unsigned ns){risc_runtime_capability_v1 g{sizeof(g)};assert(rt->acquire(cap,1,ns,&g));return g;}
extern "C" void test_atomic_bound_started(unsigned,risc_bound_key_value_v1 table){bound=table;}
extern "C" void test_atomic_bound_stopped(risc_bound_key_value_v1 table){assert(table.put(table.context,"sample","x",1)==RISC_BOUND_KEY_VALUE_CONTEXT);}
extern "C" void test_atomic_app(unsigned hook,unsigned){
 const auto* rt=risc_runtime_get_api(1);assert(rt);
 if(hook!=1){risc_runtime_capability_v1 g{sizeof(g)};assert(!rt->acquire(RISC_PREFERENCE_STORE_CAPABILITY,1,6,&g));if(hook==2&&saved.context)invalid(saved);return;}
 auto grant=acquire(rt,RISC_PREFERENCE_STORE_CAPABILITY,6);auto a=*static_cast<const risc_preference_store_v1*>(grant.api);saved=a;
 auto peer=acquire(rt,RISC_PREFERENCE_STORE_CAPABILITY,6);auto p=*static_cast<const risc_preference_store_v1*>(peer.api);
 auto other=acquire(rt,RISC_PREFERENCE_STORE_CAPABILITY,7);auto o=*static_cast<const risc_preference_store_v1*>(other.api);
 auto legacy=acquire(rt,RISC_KEY_VALUE_CAPABILITY,6);auto kv=*static_cast<const risc_key_value_v1*>(legacy.api);
 std::thread foreign([&]{invalid(a);});foreign.join();
 auto s=state(a);assert(s.namespace_id==6&&state(o).namespace_id==7);
 uint32_t size=99;char output[16];memset(output,'q',sizeof output);
 if(scenario=="bad-read"){
  values[{6,"sample"}]="one";assert(a.get(a.context,s.revision,"sample",output,sizeof output,&size)==RISC_ATOMIC_IO&&!size&&output[0]=='q');
  for(auto* g:{&grant,&peer,&other,&legacy}){assert(rt->release(g));}return;
 }
 assert(a.get(a.context,s.revision,"sample",output,sizeof output,&size)==RISC_ATOMIC_NOT_FOUND&&!size&&output[0]=='q');
 assert(!kv.put(kv.context,"sample","old",3));assert(a.get(a.context,s.revision,"sample",output,sizeof output,&size)==RISC_ATOMIC_STALE&&!size);
 s=state(a);assert(!a.get(a.context,s.revision,"sample",output,sizeof output,&size)&&size==3&&!memcmp(output,"old",3));
 assert(a.get(a.context,s.revision,"sample",nullptr,0,&size)==RISC_ATOMIC_BUFFER_SMALL&&size==3);
 assert(!bound.put(bound.context,"sample","bound",5));uint64_t token=9;
 risc_preference_store_mutation_v1 m[2]={{"sample",RISC_ATOMIC_PUT,"first",5},{"other",RISC_ATOMIC_PUT,secondInput,6}};
 assert(a.submit(a.context,s.revision,m,2,"save",4,&token)==RISC_ATOMIC_STALE&&!token);
 s=state(a);writes=0;memcpy(secondInput,"second",7);
 auto bad=m[0];bad.kind=RISC_ATOMIC_DELETE;bad.value=nullptr;bad.size=0;
 assert(a.submit(a.context,s.revision,&bad,1,"save",4,&token)==RISC_ATOMIC_INVALID&&!token&&!writes);
 bad=m[1];m[1].key=m[0].key;assert(a.submit(a.context,s.revision,m,2,"save",4,&token)==RISC_ATOMIC_INVALID&&!token&&!writes);m[1]=bad;
 bad=m[1];m[1].size=2049;assert(a.submit(a.context,s.revision,m,2,"save",4,&token)==RISC_ATOMIC_INVALID&&!token&&!writes);m[1]=bad;
 if(scenario=="partial")failWrite=2;
 if(scenario=="failed-first")failWrite=1;
 if(scenario=="busy-first"){failWrite=1;failStatus=RISC_KEY_VALUE_BUSY;}
 assert(!a.submit(a.context,s.revision,m,2,"save",4,&token)&&token);
 assert(!runtime->residentResetSafe());
 if(scenario=="uncollected")return;
 risc_preference_store_receipt_v1 receipt{sizeof(receipt)};
 assert(a.state(a.context,&s)==RISC_ATOMIC_BUSY&&p.state(p.context,&s)==RISC_ATOMIC_BUSY);
 assert(state(o).namespace_id==7);
 assert(kv.put(kv.context,"sample","blocked",7)==RISC_KEY_VALUE_BUSY);
 assert(bound.put(bound.context,"sample","blocked",7)==RISC_BOUND_KEY_VALUE_BUSY);
 assert(!rt->release(&grant));
 assert(p.collect(p.context,token,&receipt)==RISC_ATOMIC_INVALID&&p.cancel(p.context,token)==RISC_ATOMIC_INVALID);
 assert(a.collect(a.context,token+1,&receipt)==RISC_ATOMIC_INVALID);
 assert(a.cancel(a.context,token)==RISC_ATOMIC_PENDING);
 assert(!a.collect(a.context,token,&receipt)&&receipt.revision&&runtime->residentResetSafe());
 assert(receipt.status==(failWrite?RISC_ATOMIC_UNCERTAIN:RISC_ATOMIC_OK));
 assert(bool(receipt.operation_id)==!failWrite);
 if(scenario=="partial"){assert(values.at({6,"sample"})=="first"&&values.count({6,"other"})==0);}
 if(scenario=="copy")assert(values.at({6,"other"})=="second");
 assert(a.collect(a.context,token,&receipt)==RISC_ATOMIC_INVALID);
 assert(rt->release(&grant));invalid(a);
 auto fresh=acquire(rt,RISC_PREFERENCE_STORE_CAPABILITY,6);auto f=*static_cast<const risc_preference_store_v1*>(fresh.api);assert(f.context!=a.context);invalid(a);
 for(auto* g:{&fresh,&peer,&other,&legacy})assert(rt->release(g));
}
static void file(const char* name,const std::string& content){std::ofstream(directory+"/"+name)<<content;}
static void boot(unsigned api=1,unsigned ns=6){
 file("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
 file("app.json","{\"type\":\"application\",\"id\":\"preferences-main\",\"version\":\"1.0.0\",\"architecture\":\"xtensa-esp32s3\",\"file_name\":\"default.elf\",\"entry\":\"app_main\",\"requires\":[{\"capability\":\"storage.preferences\",\"api\":"+std::to_string(api)+"},{\"capability\":\"storage.key-value\",\"api\":1}]}");
 file("reader.json",R"({"type":"driver","id":"atomic-reader-1","version":"1.0.0","architecture":"xtensa-esp32s3","driver_abi":2,"file_name":"reader.elf","requires":[{"capability":"storage.key-value.bound","api":1}],"provides":[{"capability":"test.atomic.reader1","api":1}]})");
 file("boot.json","{\"board\":\"board.json\",\"default_app\":\"default.elf\",\"drivers\":[{\"manifest\":\"reader.json\",\"key_value\":[{\"key\":\"sample\",\"namespace\":6,\"access\":\"read-write\"}]}],\"app_capabilities\":[{\"manifest\":\"app.json\",\"grants\":[{\"capability\":\"storage.preferences\",\"api\":"+std::to_string(api)+",\"instance_id\":"+std::to_string(ns)+"},{\"capability\":\"storage.preferences\",\"api\":"+std::to_string(api)+",\"instance_id\":7},{\"capability\":\"storage.key-value\",\"api\":1,\"instance_id\":6}]}]}");
}
int main(int argc,char** argv){
 assert(argc==3);directory=argv[1];scenario=argv[2];boot();
 if(scenario=="admission"){
  {R r({owner,health,delay,logLine});assert(!r.prepare(directory.c_str()));}
  boot(2);{R r({owner,health,delay,logLine,nullptr,&backend});assert(!r.prepare(directory.c_str()));}
  boot(1,0);{R r({owner,health,delay,logLine,nullptr,&backend});assert(!r.prepare(directory.c_str()));}
  puts("Preference exact API/namespace/backend admission PASS");return 0;
 }
 R r({owner,health,delay,logLine,nullptr,&backend});runtime=&r;assert(r.prepare(directory.c_str()));
 if(scenario=="uncollected"){assert(!r.run()&&r.retained()&&!r.residentResetSafe());invalid(saved);puts("Preference receipt custody blocks release/exit PASS");fflush(stdout);_exit(0);}
 assert(r.run()&&!r.retained());invalid(saved);puts("Preference sync namespace revisions/copies/failures/lifecycle PASS");
}
