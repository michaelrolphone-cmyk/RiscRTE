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
extern "C" void test_radio_trace(const char* text){std::ofstream(trace,std::ios::app)<<text<<'\n';}
extern "C" unsigned test_radio_run(){return ++defaults;}
extern "C" const char* test_radio_mode(){return mode.c_str();}
extern "C" void test_radio_recover(){cleanup=true;}
extern "C" void test_radio_save(const void* app,const void* provider){appImage=app;providerImage=provider;}
static bool owner(){return true;}
static bool bind(RiscBoot::Runtime& r){return cpu->bind(r);}
static bool appExitSafe(){return cpu->appExitSafe();}
static bool providerStorageSafe(){return cpu->providerStorageSafe();}
static unsigned kvCalls=0;
static const RiscBoot::KeyValueBackend keyValue{nullptr,
 [](void*,uint32_t ns,const char* key,void* out,uint32_t capacity,uint32_t* size)->int32_t{
  assert(ns==3 && !strcmp(key,"alarm_occ") && capacity);++kvCalls;*static_cast<char*>(out)='x';*size=1;return RISC_BOUND_KEY_VALUE_OK;},
 [](void*,uint32_t ns,const char* key,const void* value,uint32_t size)->int32_t{
  assert(ns==3 && !strcmp(key,"alarm_occ") && size==1 && *static_cast<const char*>(value)=='x');++kvCalls;return RISC_BOUND_KEY_VALUE_OK;}};
static void child(){
 RiscCpu::Hardware h{};h.owner=owner;h.now=[]()->uint64_t{return 0;};h.sleep=[](uint32_t){};
 h.gpioOpen=[](uint8_t pin,bool out,bool,bool pull){assert(pin==7 && !out && pull);return true;};
 h.gpioRead=[](uint8_t,bool* level){*level=true;return true;};h.gpioWrite=[](uint8_t,bool){return true;};
 h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){return true;};h.gpioClose=[](uint8_t){return true;};
 h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){return true;};h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){return true;};h.i2cClose=[](uint8_t){return true;};
 h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){return true;};h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){return true;};h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){return true;};h.spiEnd=[](uint8_t,uint8_t,uint32_t){return true;};h.spiClose=[](uint8_t){return true;};
 h.wakeValid=[](uint8_t){return true;};h.wakeArm=[](uint8_t,bool){return true;};h.wakeClear=[](uint8_t){return true;};h.lightSleep=[](uint32_t* cause){assert(!active);*cause=RISC_LIGHT_SLEEP_WAKE_GPIO;return true;};
 h.deepWakeValid=[](uint8_t){return true;};h.deepReady=[](){assert(!active);return true;};h.deepWakeArm=[](uint8_t,bool,bool){return false;};h.deepWakeClear=[](uint8_t,bool){return true;};h.deepSleep=[](){assert(false);};
 h.radioJoin=[](const char* ssid,const char* pass){assert(!strcmp(ssid,"synthetic-test") && !strcmp(pass,"synthetic-pass"));active=true;return mode!="join-failed";};
 h.radioState=[](uint8_t* state,int8_t* rssi){*state=2;*rssi=-50;return mode!="state-failed";};
 h.radioLeave=[](){if(!cleanup)return false;active=false;return true;};
 h.radioAddresses=[](uint8_t*,uint8_t*){return mode!="addresses-failed";};h.radioIdle=[](){return !active;};
 h.radioScanStart=[](){active=true;return mode!="scan-failed";};h.radioScanPoll=[](garden_radio_scan_result_v1*){return mode!="poll-failed";};h.radioScanCancel=h.radioLeave;
 cleanup=mode!="cleanup-retained" && mode!="cleanup-retry";
 cpu=new RiscCpu::Port(h);
 auto* runtime=new RiscBoot::Runtime({owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},bind,&keyValue,appExitSafe,providerStorageSafe});
 assert(runtime->prepare(root.c_str()));
 const bool retained=mode=="join-retained" || mode=="scan-retained" || mode=="cleanup-retained" || mode=="state-failed" || mode=="addresses-failed" || mode=="poll-failed";
 // Child return reloads a fresh default, whose host-owned run counter ends the test.
 assert(runtime->run()!=retained);
 if(retained){
  assert(!cpu->appExitSafe() && !cpu->quiescent() && active);
  assert(strstr(runtime->error(),"native retention barrier"));
  Dl_info app{},provider{};assert(dladdr(appImage,&app) && dladdr(providerImage,&provider));assert(app.dli_fbase!=provider.dli_fbase);
  assert(!runtime->run() && !risc_runtime_get_api(1));test_radio_trace("retained");
 }else{assert(cpu->appExitSafe() && cpu->quiescent() && !active);delete runtime;delete cpu;test_radio_trace("clean");}
 _exit(0);
}
static void save(const char* name,const std::string& value){std::ofstream(root+"/"+name)<<value;}
static std::string read(){std::ifstream f(trace);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){
 assert(argc==2 || argc==3);root=argv[1];trace=root+"/radio-trace.txt";if(argc==3){mode=argv[2];child();}
 save("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"radio-test","revision":"unspecified","buses":[],"devices":[{"instance_id":12,"chip":{"vendor":"espressif","model":"esp32s3","revision":"unspecified"},"compatible":"espressif,esp32s3-wifi","config_type":"radio.integrated","config_version":1,"config":{"unit":0,"features":3}},{"instance_id":7,"chip":{"vendor":"test","model":"gpio","revision":"unspecified"},"compatible":"test,gpio","config_type":"gpio.bank","config_version":1,"config":{"pins":[7],"active_high":true,"pull_up":true,"debounce_us":0,"long_press_us":0,"click_min_us":0}}]})");
 save("radio.json",R"({"type":"driver","id":"radio-probe","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"radio.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.radio","api":1}],"provides":[{"capability":"test.radio","api":1}],"hardware_compatibility":[{"compatible":"espressif,esp32s3-wifi","revisions":["unspecified"],"config_type":"radio.integrated","config_version":1}]})");
 save("sleep.json",R"({"type":"driver","id":"sleep-probe","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"sleep.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.gpio","api":1}],"provides":[{"capability":"test.sleep","api":1}],"hardware_compatibility":[{"compatible":"test,gpio","revisions":["unspecified"],"config_type":"gpio.bank","config_version":1}]})");
 save("storage.json",R"({"type":"driver","id":"radio-storage-probe","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"storage.elf","requires":[{"capability":"storage.key-value.bound","api":1}],"provides":[{"capability":"test.alert","api":1}]})");
 save("app.json",R"({"type":"application","id":"radio-app","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"test.radio","api":1},{"capability":"test.sleep","api":1},{"capability":"test.alert","api":1}]})");
 save("boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"radio.json","instance_id":12},{"manifest":"sleep.json","instance_id":7},{"manifest":"storage.json","key_value":[{"key":"alarm_occ","namespace":3,"access":"read-write"}]}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"test.radio","api":1,"instance_id":12},{"capability":"test.sleep","api":1,"instance_id":7},{"capability":"test.alert","api":1,"instance_id":0}]}]})");
 for(const char* scenario:{"idle","join-failed","scan-failed","join-clean","scan-clean","cleanup-retry","join-retained","scan-retained","cleanup-retained","state-failed","addresses-failed","poll-failed"}){
  save("radio-trace.txt","");pid_t pid=fork();assert(pid>=0);if(!pid){execl(argv[0],argv[0],root.c_str(),scenario,(char*)nullptr);_exit(99);}int status=0;assert(waitpid(pid,&status,0)==pid);
  const auto output=read();if(!WIFEXITED(status) || WEXITSTATUS(status))fprintf(stderr,"%s failed:%s",scenario,output.c_str());assert(WIFEXITED(status) && !WEXITSTATUS(status));
  const bool retained=strstr(scenario,"retained") || !strcmp(scenario,"state-failed") || !strcmp(scenario,"addresses-failed") || !strcmp(scenario,"poll-failed");
  assert(output.find("APP loaded")!=std::string::npos && output.find("radio-probe started")!=std::string::npos);
  if(retained){assert(output.find("APP fini")==std::string::npos && output.find("APP unloaded")==std::string::npos && output.find("CHILD loaded")==std::string::npos && output.find("radio-probe quiesced")==std::string::npos);}
  else {assert(output.find("APP fini")!=std::string::npos && output.find("APP unloaded")!=std::string::npos && output.find("CHILD loaded")!=std::string::npos && output.find("radio-probe quiesced")!=std::string::npos);}
  printf("Real Runtime/Graph/CpuPort/dlopen radio + Light/Deep lifecycle: %s PASS\n",scenario);
 }
}
