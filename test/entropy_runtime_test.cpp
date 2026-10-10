#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#ifdef ENTROPY_ACTUAL_SERVICE
#include <RiscEntropySourceV1.h>
#endif
#include <array>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
using namespace RiscCpu;
static std::string root;
static Port* cpu;
static bool owns=true,nativeSafe=true,inFill=false;
static int calls=0,stops=0,mode=0;
static int32_t nextResult=RISC_ENTROPY_OK;
static risc_entropy_v1 tables[2]{};
using Buffer=std::array<uint8_t,34>;
static bool owner(){return owns;}
static bool idle(void*,uint64_t){return nativeSafe && !inFill;}
static bool safe(void*){return nativeSafe;}
static void unchanged(const Buffer& b){for(auto x:b)assert(x==0xa7);}
static int32_t fill(void*,uint64_t activation,void* output,uint32_t size){
 ++calls;assert(activation && output && size>=1 && size<=32);inFill=true;
 assert(!cpu->appExitSafe() && !cpu->quiescent() && !cpu->restartResourcesSafe());
 Buffer nested;nested.fill(0xa7);
 assert(tables[0].fill(tables[0].context,nested.data(),32)==RISC_ENTROPY_BUSY);unchanged(nested);
 if(mode==1)owns=false;
 if(mode==2)nativeSafe=false;
 std::memset(output,0x59,size);inFill=false;
 return mode==2?RISC_ENTROPY_RETAINED:mode==3?-99:nextResult;
}
static RiscBoot::EntropyBackend backend{nullptr,fill,idle,safe};
extern "C" bool entropy_provider_started(unsigned index,const risc_entropy_v1* table){assert(index<2&&table->context);tables[index]=*table;return mode!=4;}
extern "C" void entropy_provider_stopped(unsigned){++stops;}
static void denied(const risc_entropy_v1& table){
 if(!table.context)return;
 Buffer b;b.fill(0xa7);const int before=calls;
 assert(table.fill(table.context,b.data()+1,32)==RISC_ENTROPY_CONTEXT);unchanged(b);assert(calls==before);
}
extern "C" void entropy_run_app(){
 const auto* rt=risc_runtime_get_api(1);assert(rt);
 risc_runtime_capability_v1 first{sizeof(first)},second{sizeof(second)};
 if(mode==4){assert(!rt->acquire("test.entropy.first",1,0,&first));denied(tables[0]);return;}
 assert(rt->acquire("test.entropy.first",1,0,&first));assert(rt->acquire("test.entropy.second",1,0,&second));
 auto a=tables[0],b=tables[1];assert(a.context && b.context && a.context!=b.context);
 Buffer out;out.fill(0xa7);
 if(mode){
  assert(a.fill(a.context,out.data()+1,32)==RISC_ENTROPY_RETAINED);unchanged(out);
  owns=true;denied(a);denied(b);return;
 }
 owns=false;denied(a);owns=true;
 const int before=calls;
 assert(a.fill(a.context,nullptr,1)==RISC_ENTROPY_INVALID);
 assert(a.fill(a.context,out.data()+1,0)==RISC_ENTROPY_INVALID);
 assert(a.fill(a.context,out.data()+1,33)==RISC_ENTROPY_INVALID);
 assert(calls==before);unchanged(out);
 for(auto refusal:{RISC_ENTROPY_BUSY,RISC_ENTROPY_UNAVAILABLE,RISC_ENTROPY_INVALID}){
  nextResult=refusal;assert(a.fill(a.context,out.data()+1,32)==refusal);unchanged(out);
  assert(cpu->appExitSafe() && cpu->providerStorageSafe());
 }
 nextResult=RISC_ENTROPY_OK;
 for(uint32_t n=1;n<=32;++n){
  out.fill(0xa7);assert(a.fill(a.context,out.data()+1,n)==RISC_ENTROPY_OK);
  assert(out.front()==0xa7);for(uint32_t i=1;i<=n;++i)assert(out[i]==0x59);
  for(uint32_t i=n+1;i<out.size();++i)assert(out[i]==0xa7);
 }
#ifdef ENTROPY_ACTUAL_SERVICE
 risc_runtime_capability_v1 entropy{sizeof(entropy)};
 assert(rt->acquire("crypto.entropy",1,0,&entropy));
 const auto service=*static_cast<const risc_entropy_source_v1*>(entropy.api);
 for(auto refusal:{RISC_ENTROPY_BUSY,RISC_ENTROPY_UNAVAILABLE}){
  nextResult=refusal;out.fill(0xa7);
  assert(service.fill(service.context,out.data()+1,32)==refusal);unchanged(out);
 }
 nextResult=RISC_ENTROPY_OK;
 assert(service.fill(service.context,out.data()+1,32)==RISC_ENTROPY_SOURCE_OK);
 assert(rt->release(&entropy)); // The ordinary provider is unmapped after release; do not call copied code.
#endif
 assert(rt->release(&first));denied(a);
 assert(rt->acquire("test.entropy.first",1,0,&first));assert(tables[0].context!=a.context);denied(a);
 assert(rt->release(&first));assert(rt->release(&second));denied(tables[0]);denied(b);
}
static Hardware hardware(){
 Hardware h{};h.owner=owner;h.now=[]()->uint64_t{return 0;};h.sleep=[](uint32_t){};
 h.gpioOpen=[](uint8_t,bool,bool,bool){return true;};h.gpioWrite=[](uint8_t,bool){return true;};h.gpioRead=[](uint8_t,bool*){return true;};h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){return true;};h.gpioClose=[](uint8_t){return true;};
 h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){return true;};h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){return true;};h.i2cClose=[](uint8_t){return true;};
 h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){return true;};h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){return true;};h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){return true;};h.spiEnd=[](uint8_t,uint8_t,uint32_t){return true;};h.spiClose=[](uint8_t){return true;};h.entropy=&backend;return h;
}
static RiscBoot::Port runtimePort(){return {owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},[](RiscBoot::Runtime& r){return cpu->bind(r);},nullptr,[](){return cpu->appExitSafe();},[](){return cpu->providerStorageSafe();}};}
static void file(const char* path,const std::string& text){std::ofstream(root+"/"+path)<<text;}
int main(int argc,char** argv){
 assert(argc==2);root=argv[1];
 file("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"entropy-test","revision":"unspecified","buses":[],"devices":[]})");
 for(int index=0;index<2;++index){const std::string name=index?"second":"first";file((name+".json").c_str(),std::string(R"({"type":"driver","id":"entropy-)")+name+R"(","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":")"+name+R"(.elf","requires":[{"capability":"platform.entropy","api":1}],"provides":[{"capability":"test.entropy.)"+name+R"(","api":1}]})");}
 std::string app=R"({"type":"application","id":"entropy-app","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"test.entropy.first","api":1},{"capability":"test.entropy.second","api":1}]})";
 std::string boot=R"({"board":"board.json","default_app":"default.elf","provider_activation":"demand","drivers":[{"manifest":"first.json"},{"manifest":"second.json"}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"test.entropy.first","api":1,"instance_id":0},{"capability":"test.entropy.second","api":1,"instance_id":0}]}]})";
#ifdef ENTROPY_ACTUAL_SERVICE
 file("entropy.json",R"({"type":"driver","id":"crypto-entropy","version":"0.1.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"entropy.elf","requires":[{"capability":"platform.entropy","api":1}],"provides":[{"capability":"crypto.entropy","api":1}]})");
 app.insert(app.rfind("]}"),",{\"capability\":\"crypto.entropy\",\"api\":1}");
 boot.insert(boot.find("],\"app_capabilities\""),",{\"manifest\":\"entropy.json\"}");
 boot.insert(boot.rfind("]}]}"),",{\"capability\":\"crypto.entropy\",\"api\":1,\"instance_id\":0}");
#endif
 file("app.json",app);file("boot.json",boot);file("cohort.json","{}");
 {auto h=hardware();h.entropy=nullptr;Port p(h);cpu=&p;RiscBoot::Runtime r(runtimePort());assert(!r.prepare(root.c_str())&&calls==0);}
 {auto bad=backend;bad.safe=nullptr;auto h=hardware();h.entropy=&bad;Port p(h);cpu=&p;RiscBoot::Runtime r(runtimePort());assert(!r.prepare(root.c_str())&&calls==0);}
 {Port p(hardware());cpu=&p;RiscBoot::Runtime r(runtimePort()),candidate({});assert(r.prepare(root.c_str())&&calls==0);const bool admitted=r.validateCohort(candidate,root.c_str(),[](void*,const char*,bool){return true;},nullptr);if(!admitted)fprintf(stderr,"candidate: %s\n",candidate.error());assert(admitted&&calls==0);}
 file("app.json",R"({"type":"application","id":"entropy-app","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"platform.entropy","api":1}]})");
 file("boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"platform.entropy","api":1,"instance_id":0}]}]})");
 {Port p(hardware());cpu=&p;RiscBoot::Runtime r(runtimePort());assert(!r.prepare(root.c_str())&&calls==0);}
 file("app.json",app);file("boot.json",boot);
 for(mode=0;mode<5;++mode){
  const auto child=fork();assert(child>=0);
  if(!child){Port p(hardware());cpu=&p;RiscBoot::Runtime r(runtimePort());assert(r.prepare(root.c_str())&&calls==0);assert(r.run()==(mode==0 || mode==4));denied(tables[0]);assert(mode==0?stops==3:mode==4?stops==1:stops==0);_exit(0);}
  int status;assert(waitpid(child,&status,0)==child);if(!(WIFEXITED(status)&&WEXITSTATUS(status)==0))fprintf(stderr,"mode %d status %d\n",mode,status);assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);
 }
 puts("Runtime entropy: opt-in, zero-I/O admission, provider generations, bounded copies, BUSY, owner loss and retained cleanup PASS");
}
