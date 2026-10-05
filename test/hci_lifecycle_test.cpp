#include "ports/esp32s3/CpuPort.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <string>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/wait.h>
static std::string root,mode,trace;
static RiscCpu::Port* cpu;
static bool active=false,cleanup=true;
static const void* appImage;static const void* providerImage;
static unsigned defaults=0;
extern "C" void test_hci_trace(const char* text){std::ofstream(trace,std::ios::app)<<text<<'\n';}
extern "C" unsigned test_hci_run(){return ++defaults;}
extern "C" const char* test_hci_mode(){return mode.c_str();}
extern "C" void test_hci_recover(){cleanup=true;}
extern "C" void test_hci_save(const void* app,const void* provider){appImage=app;providerImage=provider;}
static bool owner(){return true;}
static bool bind(RiscBoot::Runtime& r){return cpu->bind(r);}
static bool appExitSafe(){return cpu->appExitSafe();}
static bool providerStorageSafe(){return cpu->providerStorageSafe();}
static void child(){
 RiscCpu::Hardware h{};h.owner=owner;h.now=[]()->uint64_t{return 0;};h.sleep=[](uint32_t){};
 h.gpioOpen=[](uint8_t pin,bool out,bool,bool pull){assert(pin==7 && !out && pull);return true;};
 h.gpioRead=[](uint8_t,bool* level){*level=true;return true;};h.gpioWrite=[](uint8_t,bool){return true;};
 h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){return true;};h.gpioClose=[](uint8_t){return true;};
 h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){return true;};h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){return true;};h.i2cClose=[](uint8_t){return true;};
 h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){return true;};h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){return true;};h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){return true;};h.spiEnd=[](uint8_t,uint8_t,uint32_t){return true;};h.spiClose=[](uint8_t){return true;};
 h.wakeValid=[](uint8_t){return true;};h.wakeArm=[](uint8_t,bool){return true;};h.wakeClear=[](uint8_t){return true;};h.lightSleep=[](uint32_t* cause){assert(!active);*cause=RISC_LIGHT_SLEEP_WAKE_GPIO;return true;};
 h.deepWakeValid=[](uint8_t){return true;};h.deepReady=[](){assert(!active);return true;};h.deepWakeArm=[](uint8_t,bool,bool){return false;};h.deepWakeClear=[](uint8_t,bool){return true;};h.deepSleep=[](){assert(false);};
 h.hciOpen=[](){active=true;return mode!="open-clean" && mode!="open-retained";};
 h.hciClose=[](){if(!cleanup)return false;active=false;return true;};
 h.hciIdle=[](){return !active;};h.hciSafe=[](){return true;};
 h.hciSend=[](uint8_t,const uint8_t*,size_t,uint32_t){return true;};
 h.hciReceive=[](uint8_t*,uint8_t*,size_t,size_t* n,uint32_t){*n=0;return mode!="poll-retained" || !cleanup;};
 cleanup=mode!="cleanup-retained" && mode!="cleanup-retry" && mode!="open-retained" && mode!="poll-retained";
 cpu=new RiscCpu::Port(h);
 auto* runtime=new RiscBoot::Runtime({owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},bind,nullptr,appExitSafe,providerStorageSafe});
 assert(runtime->prepare(root.c_str()));
 const bool retained=mode=="open-retained" || mode=="cleanup-retained" || mode=="poll-retained";
 // Child return reloads a fresh default, whose host-owned run counter ends the test.
 assert(runtime->run()!=retained);
 if(retained){
  assert(!cpu->appExitSafe() && !cpu->quiescent() && active);
  assert(strstr(runtime->error(),"native retention barrier"));
  Dl_info app{},provider{};assert(dladdr(appImage,&app) && dladdr(providerImage,&provider));assert(app.dli_fbase!=provider.dli_fbase);
  assert(!runtime->run() && !risc_runtime_get_api(1));test_hci_trace("retained");
 }else{assert(cpu->appExitSafe() && cpu->quiescent() && !active);delete runtime;delete cpu;test_hci_trace("clean");}
 _exit(0);
}
static void save(const char* name,const std::string& value){std::ofstream(root+"/"+name)<<value;}
static std::string read(){std::ifstream f(trace);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){
 assert(argc==2 || argc==3);root=argv[1];trace=root+"/hci-trace.txt";if(argc==3){mode=argv[2];child();}
 save("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"hci-test","revision":"unspecified","buses":[],"devices":[{"instance_id":12,"chip":{"vendor":"espressif","model":"esp32s3","revision":"unspecified"},"compatible":"espressif,esp32s3-ble","config_type":"radio.integrated","config_version":1,"config":{"unit":0,"features":1}}]})");
 save("hci.json",R"({"type":"driver","id":"hci-probe","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"hci.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.hci.controller","api":1}],"provides":[{"capability":"test.hci","api":1}],"hardware_compatibility":[{"compatible":"espressif,esp32s3-ble","revisions":["unspecified"],"config_type":"radio.integrated","config_version":1}]})");
 save("app.json",R"({"type":"application","id":"hci-app","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"test.hci","api":1}]})");
 save("child.json",R"({"type":"application","id":"hci-child","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"child.elf","entry":"app_main","requires":[{"capability":"test.hci","api":1}]})");
 save("boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"hci.json","instance_id":12}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"test.hci","api":1,"instance_id":12}]},{"manifest":"child.json","grants":[{"capability":"test.hci","api":1,"instance_id":12}]}]})");
 for(const char* scenario:{"handoff","open-clean","open-retained","cleanup-retained","cleanup-retry","poll-retained"}){
  save("hci-trace.txt","");pid_t pid=fork();assert(pid>=0);if(!pid){execl(argv[0],argv[0],root.c_str(),scenario,(char*)nullptr);_exit(99);}int status=0;assert(waitpid(pid,&status,0)==pid);
  const auto output=read();if(!WIFEXITED(status) || WEXITSTATUS(status))fprintf(stderr,"%s failed:%s",scenario,output.c_str());assert(WIFEXITED(status) && !WEXITSTATUS(status));
  const bool retained=strstr(scenario,"retained");
  assert(output.find("APP loaded")!=std::string::npos && output.find("PROVIDER started")!=std::string::npos);
  if(retained){assert(output.find("APP fini")==std::string::npos && output.find("APP unloaded")==std::string::npos && output.find("CHILD loaded")==std::string::npos && output.find("PROVIDER quiesced")==std::string::npos);}
  else {assert(output.find("APP fini")!=std::string::npos && output.find("APP unloaded")!=std::string::npos && output.find("CHILD loaded")!=std::string::npos && output.find("PROVIDER quiesced")!=std::string::npos);}
  printf("Real Runtime/Graph/CpuPort/dlopen HCI lifecycle: %s PASS\n",scenario);
 }
}
