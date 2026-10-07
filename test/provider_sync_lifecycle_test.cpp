#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <fstream>
#include <string>
#include <cstdio>
using namespace RiscCpu;
static Port* activePort;
static unsigned io=0;
static bool owner(){return true;}
static bool bind(RiscBoot::Runtime&r){return activePort->bind(r);}
static Hardware hardware(){
 Hardware h{};h.owner=owner;h.now=[]()->uint64_t{return 0;};h.sleep=[](uint32_t){++io;};
 h.gpioOpen=[](uint8_t,bool,bool,bool){++io;return true;};h.gpioWrite=[](uint8_t,bool){++io;return true;};
 h.gpioRead=[](uint8_t,bool*){++io;return true;};h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){++io;return true;};h.gpioClose=[](uint8_t){++io;return true;};
 h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){++io;return true;};h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){++io;return true;};h.i2cClose=[](uint8_t){++io;return true;};
 h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){++io;return true;};h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){++io;return true;};h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){++io;return true;};h.spiEnd=[](uint8_t,uint8_t,uint32_t){++io;return true;};h.spiClose=[](uint8_t){++io;return true;};
 h.hciOpen=[](){++io;return true;};h.hciClose=[](){++io;return true;};
 h.hciSend=[](uint8_t,const uint8_t*,size_t,uint32_t){++io;return true;};h.hciReceive=[](uint8_t*,uint8_t*,size_t,size_t*,uint32_t){++io;return true;};
 h.hciIdle=[](){return true;};h.hciSafe=[](){return true;};return h;
}

#include <dlfcn.h>
#include <unistd.h>
#include <sys/wait.h>
static std::string selectedMode,tracePath;
static const void *appImage,*providerImage;
extern "C" const char* test_sync_mode(){return selectedMode.c_str();}
extern "C" void test_sync_trace(const char* text){std::ofstream(tracePath,std::ios::app)<<text<<'\n';}
extern "C" void test_sync_save(const void* app,const void* provider){appImage=app;providerImage=provider;}
static bool appSafe(){return activePort->appExitSafe();}
static bool storageSafe(){return activePort->providerStorageSafe();}
int main(int argc,char**argv){
 assert(argc==2);const std::string root=argv[1];tracePath=root+"/sync-trace.txt";
 auto save=[&](const char*path,const std::string&data){std::ofstream(root+"/"+path)<<data;};
 save("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"sync-test","revision":"unspecified","buses":[],"devices":[{"instance_id":1,"chip":{"vendor":"test","model":"gpio","revision":"unspecified"},"compatible":"test,gpio","config_type":"gpio.bank","config_version":1,"config":{"pins":[8],"active_high":true,"pull_up":false,"debounce_us":0,"long_press_us":0,"click_min_us":0}}]})");
 save("driver.json",R"({"type":"driver","id":"sync-probe","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"driver.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.sync","api":1}],"provides":[{"capability":"test.sync","api":1}],"hardware_compatibility":[{"compatible":"test,gpio","revisions":["unspecified"],"config_type":"gpio.bank","config_version":1}]})");
 save("app.json",R"({"type":"application","id":"sync-app","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"test.sync","api":1}]})");
 save("boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"driver.json","instance_id":1}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"test.sync","api":1,"instance_id":1}]}]})");
 for(const char* mode:{"clean","retained"}){
  selectedMode=mode;save("sync-trace.txt","");const pid_t child=fork();assert(child>=0);
  if(!child){
   auto* port=new Port(hardware());activePort=port;
   auto* runtime=new RiscBoot::Runtime({owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},bind,nullptr,appSafe,storageSafe});
   if(!runtime->prepare(root.c_str())){std::fprintf(stderr,"prepare: %s\n",runtime->error());assert(false);}const bool retained=selectedMode=="retained";
   assert(runtime->run()!=retained);
   if(retained){
    assert(runtime->retained() && !port->appExitSafe() && !port->quiescent());
    Dl_info app{},provider{};assert(dladdr(appImage,&app) && dladdr(providerImage,&provider));
    assert(app.dli_fbase!=provider.dli_fbase && !runtime->run());
   }else{assert(port->quiescent() && port->appExitSafe());delete runtime;delete port;}
   assert(!io);_exit(0);
  }
  int status=0;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
  std::ifstream f(tracePath);std::string events{std::istreambuf_iterator<char>(f),{}};
  if(selectedMode=="retained")assert(events.find("unloaded")==std::string::npos);
  else assert(events.find("app-unloaded")!=std::string::npos && events.find("provider-unloaded")!=std::string::npos);
 }
 puts("Real Runtime/Graph/dlopen sync: clean teardown and held-lock app/provider retention PASS");
}
