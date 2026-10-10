#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
using namespace RiscCpu;
static std::string root;
static Port* cpu;
static bool owns=true,storageSafe=true,nativeSafe=true;
static int calls=0,stops=0,mode=0;
static uint64_t heldOwner=0,heldHandle=0,nextHandle=0;
static risc_tcp_listener_v1 tables[2]{};
static risc_tcp_listen_v1 request{sizeof(request),{0,0,0,0},8080,0};
static bool owner(){return owns;}
static int32_t listen(void*,uint64_t owner,const risc_tcp_listen_v1*,uint64_t* out){++calls;if(heldHandle)return RISC_TCP_LIMIT;heldOwner=owner;*out=heldHandle=++nextHandle;return 0;}
static int32_t accept(void*,uint64_t owner,uint64_t handle,uint64_t* out){++calls;*out=0;return owner==heldOwner&&handle==heldHandle?RISC_TCP_WOULD_BLOCK:RISC_TCP_CONTEXT;}
static int32_t read(void*,uint64_t owner,uint64_t handle,void*,uint32_t,uint32_t* out){++calls;*out=0;return owner==heldOwner&&handle==heldHandle?RISC_TCP_WOULD_BLOCK:RISC_TCP_CONTEXT;}
static int32_t write(void*,uint64_t owner,uint64_t handle,const void*,uint32_t,uint32_t* out){return read(nullptr,owner,handle,nullptr,0,out);}
static int32_t close(void*,uint64_t owner,uint64_t handle){++calls;if(owner!=heldOwner||handle!=heldHandle)return RISC_TCP_CONTEXT;if(mode==2){nativeSafe=false;return RISC_TCP_RETAINED;}heldHandle=heldOwner=0;return 0;}
static bool idle(void*,uint64_t owner){return nativeSafe&&(!heldHandle||(owner&&owner!=heldOwner));}
static bool safe(void*){return nativeSafe;}
static RiscBoot::TcpListenerBackend backend{nullptr,listen,accept,read,write,close,idle,safe};
extern "C" bool tcp_provider_started(unsigned index,const risc_tcp_listener_v1* table){assert(index<2&&table->context);tables[index]=*table;
 if(mode==4&&index==0){uint64_t handle=0;assert(table->listen(table->context,&request,&handle)==0);return false;}
 return true;}
extern "C" void tcp_provider_stopped(unsigned){++stops;}
static void denied(const risc_tcp_listener_v1& a){uint64_t out=99;uint32_t n=99;char bytes[8]{};const int before=calls;
 assert(a.listen(a.context,&request,&out)==RISC_TCP_CONTEXT&&out==0);
 assert(a.accept(a.context,1,&out)==RISC_TCP_CONTEXT&&out==0);
 assert(a.read(a.context,1,bytes,8,&n)==RISC_TCP_CONTEXT&&n==0);
 assert(a.write(a.context,1,bytes,8,&n)==RISC_TCP_CONTEXT&&n==0);
 assert(a.close(a.context,1)==RISC_TCP_CONTEXT&&calls==before);
}
extern "C" void tcp_run_app(){
 const auto* rt=risc_runtime_get_api(1);assert(rt);
 risc_runtime_capability_v1 first{sizeof(first)},second{sizeof(second)};
 if(mode==4){assert(!rt->acquire("test.tcp.first",1,0,&first));denied(tables[0]);return;}
 assert(rt->acquire("test.tcp.first",1,0,&first));assert(rt->acquire("test.tcp.second",1,0,&second));
 auto a=tables[0],b=tables[1];assert(a.context&&b.context&&a.context!=b.context);
 uint64_t handle=0,out=99;uint32_t n=99;char bytes[8]{};
 assert(a.listen(a.context,&request,&handle)==0&&heldOwner==reinterpret_cast<uintptr_t>(a.context));
 assert(b.accept(b.context,handle,&out)==RISC_TCP_CONTEXT&&out==0);
 assert(b.close(b.context,handle)==RISC_TCP_CONTEXT&&heldHandle==handle);
 assert(!cpu->appExitSafe()&&!cpu->quiescent()&&!cpu->restartResourcesSafe()&&cpu->providerStorageSafe());
 Port::Gpio gpio{};gpio.port=cpu;risc_light_sleep_result_v1 light{sizeof(light)};
 assert(Port::lightSleepImpl(&gpio,1,false,0,&light)==RISC_LIGHT_SLEEP_BUSY);
 assert(Port::deepSleepImpl(&gpio,1,false,0)==RISC_DEEP_SLEEP_BUSY);
 assert(Port::sleepSetImpl(&gpio,1,false,0,false,&light)==RISC_LIGHT_SLEEP_BUSY);
 assert(Port::sleepSetImpl(&gpio,1,false,0,true,&light)==RISC_DEEP_SLEEP_BUSY);
 owns=false;denied(a);owns=true;
 if(mode==1){
  int before=calls;cpu->sleepRetained_=true;
  assert(a.read(a.context,handle,bytes,8,&n)==RISC_TCP_CONTEXT&&n==0&&calls==before);cpu->sleepRetained_=false;
  denied(a);return;
 }
 if(mode==2){assert(a.close(a.context,handle)==RISC_TCP_RETAINED);denied(a);return;}
 if(mode==3)return; // A live listener at app return retains the invocation.
 assert(a.close(a.context,handle)==0);
 assert(cpu->appExitSafe()&&cpu->quiescent()&&cpu->restartResourcesSafe());
 assert(rt->release(&first));denied(a);
 assert(rt->acquire("test.tcp.first",1,0,&first));
 assert(tables[0].context!=a.context);denied(a);
 assert(tables[0].listen(tables[0].context,&request,&handle)==0);
 assert(tables[0].close(tables[0].context,handle)==0);
 assert(rt->release(&first));assert(rt->release(&second));
 denied(tables[0]);denied(b);
}
static Hardware hardware(){
 Hardware h{};h.owner=owner;h.now=[]()->uint64_t{return 0;};h.sleep=[](uint32_t){};
 h.gpioOpen=[](uint8_t,bool,bool,bool){return true;};h.gpioWrite=[](uint8_t,bool){return true;};h.gpioRead=[](uint8_t,bool*){return true;};h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){return true;};h.gpioClose=[](uint8_t){return true;};
 h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){return true;};h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){return true;};h.i2cClose=[](uint8_t){return true;};
 h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){return true;};h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){return true;};h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){return true;};h.spiEnd=[](uint8_t,uint8_t,uint32_t){return true;};h.spiClose=[](uint8_t){return true;};h.tcpListener=&backend;return h;
}
static RiscBoot::Port runtimePort(){return {owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},[](RiscBoot::Runtime& r){return cpu->bind(r);},nullptr,[](){return cpu->appExitSafe();},[](){return storageSafe&&cpu->providerStorageSafe();}};}
static void file(const char* path,const std::string& text){std::ofstream(root+"/"+path)<<text;}
int main(int argc,char** argv){
 assert(argc==2);root=argv[1];
 file("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"tcp-test","revision":"unspecified","buses":[],"devices":[]})");
 for(int index=0;index<2;++index){const std::string name=index?"second":"first";file((name+".json").c_str(),std::string(R"({"type":"driver","id":"tcp-)")+name+R"(","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":")"+name+R"(.elf","requires":[{"capability":"platform.tcp-listener","api":1}],"provides":[{"capability":"test.tcp.)"+name+R"(","api":1}]})");}
 file("app.json",R"({"type":"application","id":"tcp-app","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"test.tcp.first","api":1},{"capability":"test.tcp.second","api":1}]})");
 file("boot.json",R"({"board":"board.json","default_app":"default.elf","provider_activation":"demand","drivers":[{"manifest":"first.json"},{"manifest":"second.json"}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"test.tcp.first","api":1,"instance_id":0},{"capability":"test.tcp.second","api":1,"instance_id":0}]}]})");
 {auto h=hardware();h.tcpListener=nullptr;Port p(h);cpu=&p;RiscBoot::Runtime r(runtimePort());assert(!r.prepare(root.c_str())&&calls==0);}
 {auto bad=backend;bad.close=nullptr;auto h=hardware();h.tcpListener=&bad;Port p(h);cpu=&p;RiscBoot::Runtime r(runtimePort());assert(!r.prepare(root.c_str())&&calls==0);}
 file("cohort.json","{}");
 {Port p(hardware());cpu=&p;RiscBoot::Runtime current(runtimePort()),candidate({});
  assert(current.prepare(root.c_str())&&calls==0);
  const bool admitted=current.validateCohort(candidate,root.c_str(),[](void*,const char*,bool){return true;},nullptr);
  if(!admitted)fprintf(stderr,"candidate: %s\n",candidate.error());
  assert(admitted&&calls==0);

 }
 // A native-first graph with no TCP provider must admit a later selected
 // graph through the same backend; disabled native firmware must reject it.
 const std::string oldRoot=root+"-old";std::filesystem::create_directory(oldRoot);
 for(const char* path:{"board.json","cohort.json","default.elf"})std::filesystem::copy_file(root+"/"+path,oldRoot+"/"+path);
 std::ofstream(oldRoot+"/app.json")<<R"({"type":"application","id":"tcp-app","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[]})";
 std::ofstream(oldRoot+"/boot.json")<<R"({"board":"board.json","default_app":"default.elf","drivers":[],"app_capabilities":[{"manifest":"app.json","grants":[]}]})";
 for(bool enabled:{false,true}){auto h=hardware();if(!enabled)h.tcpListener=nullptr;Port p(h);cpu=&p;RiscBoot::Runtime current(runtimePort()),candidate({});
  assert(current.prepare(oldRoot.c_str()));
  assert(current.validateCohort(candidate,root.c_str(),[](void*,const char*,bool){return true;},nullptr)==enabled&&calls==0);
 }
 const std::string normalBoot=R"({"board":"board.json","default_app":"default.elf","provider_activation":"demand","drivers":[{"manifest":"first.json"},{"manifest":"second.json"}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"test.tcp.first","api":1,"instance_id":0},{"capability":"test.tcp.second","api":1,"instance_id":0}]}]})";
 const std::string normalApp=R"({"type":"application","id":"tcp-app","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"test.tcp.first","api":1},{"capability":"test.tcp.second","api":1}]})";
 file("app.json",R"({"type":"application","id":"tcp-app","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"platform.tcp-listener","api":1}]})");
 file("boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"platform.tcp-listener","api":1,"instance_id":0}]}]})");
 {Port p(hardware());cpu=&p;RiscBoot::Runtime r(runtimePort());assert(!r.prepare(root.c_str())&&calls==0);}
 file("app.json",normalApp);file("boot.json",normalBoot);
 for(mode=0;mode<5;++mode){
  const auto child=fork();assert(child>=0);
  if(!child){Port p(hardware());cpu=&p;RiscBoot::Runtime r(runtimePort());assert(r.prepare(root.c_str())&&calls==0);assert(r.run()==(mode==0));denied(tables[0]);assert(mode==0?stops==3:stops==0);_exit(0);}
  int status;assert(waitpid(child,&status,0)==child);assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);
 }
 puts("Runtime TCP: opt-in/admission, provider generation ownership, raw app denial and retention fences PASS");
}
