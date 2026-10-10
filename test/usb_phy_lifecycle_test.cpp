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
static bool active=false;
extern "C" void test_native_usb_start();
extern "C" bool test_native_usb_idle();
extern "C" bool test_native_usb_suspend();
extern "C" bool test_native_usb_resume(bool);
extern "C" void test_native_usb_finish();
static const void* appImage;static const void* providerImage;
static unsigned defaults=0;
extern "C" void test_usb_phy_trace(const char* text){std::ofstream(trace,std::ios::app)<<text<<'\n';}
extern "C" unsigned test_radio_run(){return ++defaults;}
extern "C" const char* test_usb_phy_mode(){return mode.c_str();}
extern "C" void test_usb_phy_dirty(bool value){active=value;}
extern "C" void test_usb_phy_save(const void* app,const void* provider){appImage=app;providerImage=provider;}
static bool owner(){return true;}
static bool bind(RiscBoot::Runtime& r){return cpu->bind(r);}
static bool appExitSafe(){return cpu->appExitSafe();}
static bool providerStorageSafe(){return cpu->providerStorageSafe();}
static void child(){
 test_native_usb_start();
 RiscCpu::Hardware h{};h.owner=owner;h.now=[]()->uint64_t{return 0;};h.sleep=[](uint32_t){};
 h.gpioOpen=[](uint8_t pin,bool out,bool,bool pull){assert(pin==7 && !out && pull);return true;};
 h.gpioRead=[](uint8_t,bool* level){*level=true;return true;};h.gpioWrite=[](uint8_t,bool){return true;};
 h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){return true;};h.gpioClose=[](uint8_t){return true;};
 h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){return true;};h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){return true;};h.i2cClose=[](uint8_t){return true;};
 h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){return true;};h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){return true;};h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){return true;};h.spiEnd=[](uint8_t,uint8_t,uint32_t){return true;};h.spiClose=[](uint8_t){return true;};
 h.wakeValid=[](uint8_t){return true;};h.wakeArm=[](uint8_t,bool){return true;};h.wakeClear=[](uint8_t){return true;};h.lightSleep=[](uint32_t* cause){assert(!active);*cause=RISC_LIGHT_SLEEP_WAKE_GPIO;return true;};
 h.deepWakeValid=[](uint8_t){return true;};h.deepReady=[](){assert(!active);return true;};h.deepWakeArm=[](uint8_t,bool,bool){return false;};h.deepWakeClear=[](uint8_t,bool){return true;};h.deepSleep=[](){assert(false);};
 h.usbPhyIdle=test_native_usb_idle;
 h.usbPhySuspend=[](){if(active)return false;const bool ok=test_native_usb_suspend();return ok && mode!="partial";};
 h.usbPhyResume=[](){return test_native_usb_resume(active);};
 cpu=new RiscCpu::Port(h);
 auto* runtime=new RiscBoot::Runtime({owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},bind,nullptr,appExitSafe,providerStorageSafe});
 assert(runtime->prepare(root.c_str()));
 const bool retained=mode=="retained" || mode=="unreleased" || mode=="partial";
 // Child return reloads a fresh default, whose host-owned run counter ends the test.
 assert(runtime->run()!=retained);
 if(retained){
  assert(!cpu->appExitSafe() && !cpu->quiescent());
  assert(strstr(runtime->error(),"native retention barrier"));
  Dl_info app{},provider{};assert(dladdr(appImage,&app) && dladdr(providerImage,&provider));assert(app.dli_fbase!=provider.dli_fbase);
  assert(!runtime->run() && !risc_runtime_get_api(1));test_usb_phy_trace("retained");
 }else{assert(cpu->appExitSafe() && cpu->quiescent() && !active);delete runtime;delete cpu;test_native_usb_finish();test_usb_phy_trace("clean");}
 _exit(0);
}
static void save(const char* name,const std::string& value){std::ofstream(root+"/"+name)<<value;}
static std::string read(){std::ifstream f(trace);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){
 assert(argc==2 || argc==3);root=argv[1];trace=root+"/usb-phy-trace.txt";if(argc==3){mode=argv[2];child();}
 save("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"usb-phy-test","revision":"unspecified","buses":[],"devices":[]})");
 save("usb-phy.json",R"({"type":"driver","id":"usb-phy-probe","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"usb-phy.elf","requires":[{"capability":"platform.usb.phy.resource","api":1}],"provides":[{"capability":"test.usb-phy","api":1}]})");
 save("app.json",R"({"type":"application","id":"usb-phy-app","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"test.usb-phy","api":1}]})");
 save("boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"usb-phy.json"}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"test.usb-phy","api":1,"instance_id":0}]}]})");
 for(const char* scenario:{"lazy","refused","clean","retry","retained","unreleased","partial"}){
  save("usb-phy-trace.txt","");pid_t pid=fork();assert(pid>=0);if(!pid){execl(argv[0],argv[0],root.c_str(),scenario,(char*)nullptr);_exit(99);}int status=0;assert(waitpid(pid,&status,0)==pid);
  const auto output=read();if(!WIFEXITED(status) || WEXITSTATUS(status))fprintf(stderr,"%s failed:%s",scenario,output.c_str());assert(WIFEXITED(status) && !WEXITSTATUS(status));
  const bool retained=!strcmp(scenario,"retained") || !strcmp(scenario,"unreleased") || !strcmp(scenario,"partial");
  assert(output.find("app init")!=std::string::npos && output.find("provider start")!=std::string::npos);
  for(const char* phase:{"app fini","app unload","provider quiesce","provider stop","provider unload"})
   assert((output.find(phase)==std::string::npos)==retained);
  printf("Real Runtime/Graph/CpuPort/pinned HWCDC USB PHY retention lifecycle: %s PASS\n",scenario);
 }
}
