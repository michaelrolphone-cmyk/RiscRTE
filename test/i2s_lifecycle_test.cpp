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
static bool active[2]{},cleanup=true;
static const void* appImage;static const void* providerImage;
static unsigned defaults=0,kvCalls=0,transferCalls=0;
extern "C" void test_i2s_trace(const char* text){std::ofstream(trace,std::ios::app)<<text<<'\n';}
extern "C" unsigned test_i2s_run(){return ++defaults;}
extern "C" const char* test_i2s_mode(){return mode.c_str();}
extern "C" void test_i2s_recover(){cleanup=true;}
extern "C" void test_i2s_save(const void* app,const void* provider){appImage=app;providerImage=provider;}
static bool owner(){return true;}
static bool bind(RiscBoot::Runtime& r){return cpu->bind(r);}
static bool appExitSafe(){return cpu->appExitSafe();}
static bool providerStorageSafe(){return cpu->providerStorageSafe();}
static bool has(const char* s){return mode.find(s)!=std::string::npos;}
static bool transfer(uint8_t unit,size_t frames,size_t* done,uint32_t ms){
 assert(active[unit] && frames==256 && ms==40);++transferCalls;
 // Reentrant storage/exit checks during the native callback must fail closed.
 assert(!cpu->providerStorageSafe() && !cpu->appExitSafe());
 *done=has("oversize")?frames+1:has("partial")?32:has("empty")?0:frames;
 return !has("error");
}
static const RiscBoot::KeyValueBackend keyValue{nullptr,
 [](void*,uint32_t ns,const char* key,void* out,uint32_t capacity,uint32_t* size)->int32_t{
  assert(ns==3 && !strcmp(key,"alarm_occ") && capacity);++kvCalls;*static_cast<char*>(out)='x';*size=1;return RISC_BOUND_KEY_VALUE_OK;},
 [](void*,uint32_t ns,const char* key,const void* value,uint32_t size)->int32_t{
  assert(ns==3 && !strcmp(key,"alarm_occ") && size==1 && *static_cast<const char*>(value)=='x');++kvCalls;return RISC_BOUND_KEY_VALUE_OK;}};
static void child(){
 RiscCpu::Hardware h{};h.owner=owner;h.now=[]()->uint64_t{return 0;};h.sleep=[](uint32_t){};
 h.gpioOpen=[](uint8_t pin,bool out,bool,bool pull){assert(pin==4 && !out && pull);return true;};
 h.gpioRead=[](uint8_t,bool* level){*level=true;return true;};h.gpioWrite=[](uint8_t,bool){return true;};
 h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){return true;};h.gpioClose=[](uint8_t){return true;};
 h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){return true;};h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){return true;};h.i2cClose=[](uint8_t){return true;};
 h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){return true;};h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){return true;};h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){return true;};h.spiEnd=[](uint8_t,uint8_t,uint32_t){return true;};h.spiClose=[](uint8_t){return true;};
 h.wakeValid=[](uint8_t){return true;};h.wakeArm=[](uint8_t,bool){return true;};h.wakeClear=[](uint8_t){return true;};h.lightSleep=[](uint32_t* cause){assert(!active[0] && !active[1]);*cause=RISC_LIGHT_SLEEP_WAKE_GPIO;return true;};
 h.deepWakeValid=[](uint8_t){return true;};h.deepReady=[](){assert(!active[0] && !active[1]);return true;};h.deepWakeArm=[](uint8_t,bool,bool){return false;};h.deepWakeClear=[](uint8_t,bool){return true;};h.deepSleep=[](){assert(false);};
 h.i2sOpen=[](uint8_t unit,uint8_t clk,uint8_t ws,uint8_t data,uint32_t rate){assert(unit==1 && clk==7 && ws==8 && data==9 && rate==16000 && !active[unit]);active[unit]=true;return !has("open-");};
 h.i2sOpenRx=[](uint8_t unit,uint8_t clk,uint8_t data,uint32_t rate){assert(unit==0 && clk==10 && data==11 && rate==16000 && !active[unit]);active[unit]=true;return !has("open-");};
 h.i2sWrite=[](uint8_t unit,const int16_t*,size_t frames,size_t* done,uint32_t ms){assert(unit==1);return transfer(unit,frames,done,ms);};
 h.i2sRead=[](uint8_t unit,int16_t*,size_t frames,size_t* done,uint32_t ms){assert(unit==0);return transfer(unit,frames,done,ms);};
 h.i2sClose=[](uint8_t unit){if(!cleanup)return false;active[unit]=false;return true;};
 cleanup=!has("close-") && !has("open-retained");cpu=new RiscCpu::Port(h);
 auto* runtime=new RiscBoot::Runtime({owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},bind,&keyValue,appExitSafe,providerStorageSafe});
 assert(runtime->prepare(root.c_str()));const bool retained=has("retained");
 assert(runtime->run()!=retained);
 if(!has("open-"))assert(transferCalls==((mode.find("rx-")==0&&(has("partial")||has("empty")))?100u:1u));else assert(!transferCalls);
 assert(kvCalls>=4);
 if(retained){
  assert(!cpu->appExitSafe() && !cpu->quiescent() && (active[0] || active[1]));
  assert(strstr(runtime->error(),"native retention barrier"));
  Dl_info app{},provider{};assert(dladdr(appImage,&app) && dladdr(providerImage,&provider));assert(app.dli_fbase!=provider.dli_fbase);
  assert(!runtime->run() && !risc_runtime_get_api(1));test_i2s_trace("retained");
 }else{assert(cpu->appExitSafe() && !active[0] && !active[1]);delete runtime;assert(cpu->quiescent());delete cpu;test_i2s_trace("clean");}
 _exit(0);
}
static void save(const char* name,const std::string& value){std::ofstream(root+"/"+name)<<value;}
static std::string read(){std::ifstream f(trace);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){
 assert(argc==2 || argc==3);root=argv[1];trace=root+"/i2s-trace.txt";if(argc==3){mode=argv[2];child();}
 save("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"i2s-test","revision":"unspecified","buses":[],"devices":[{"instance_id":12,"chip":{"vendor":"test","model":"tx","revision":"unspecified"},"compatible":"test,audio","config_type":"audio.i2s","config_version":1,"config":{"controller":1,"pdm_rx":false,"bclk":7,"ws":8,"data":9}},{"instance_id":13,"chip":{"vendor":"test","model":"rx","revision":"unspecified"},"compatible":"test,audio","config_type":"audio.i2s","config_version":1,"config":{"controller":0,"pdm_rx":true,"bclk":10,"ws":-1,"data":11}},{"instance_id":7,"chip":{"vendor":"test","model":"gpio","revision":"unspecified"},"compatible":"test,gpio","config_type":"gpio.bank","config_version":1,"config":{"pins":[4],"active_high":true,"pull_up":true,"debounce_us":0,"long_press_us":0,"click_min_us":0}}]})");
 save("audio.json",R"({"type":"driver","id":"i2s-probe","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"audio.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.i2s.controller","api":1}],"provides":[{"capability":"test.audio","api":1}],"hardware_compatibility":[{"compatible":"test,audio","revisions":["unspecified"],"config_type":"audio.i2s","config_version":1}]})");
 save("sleep.json",R"({"type":"driver","id":"i2s-sleep-probe","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"sleep.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.gpio","api":1}],"provides":[{"capability":"test.sleep","api":1}],"hardware_compatibility":[{"compatible":"test,gpio","revisions":["unspecified"],"config_type":"gpio.bank","config_version":1}]})");
 save("storage.json",R"({"type":"driver","id":"i2s-storage-probe","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"storage.elf","requires":[{"capability":"storage.key-value.bound","api":1}],"provides":[{"capability":"test.alert","api":1}]})");
 save("app.json",R"({"type":"application","id":"i2s-app","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"test.audio","api":1},{"capability":"test.sleep","api":1},{"capability":"test.alert","api":1}]})");
 for(const char* direction:{"tx-","rx-"})for(const char* suffix:{"clean","retained","open-clean","open-retained","error-clean","partial-clean","empty-clean","error-retained","partial-retained","empty-retained","oversize-retained","close-retry","close-retained"}){
  mode=std::string(direction)+suffix;
  save("boot.json",std::string(R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"audio.json","instance_id":12},{"manifest":"audio.json","instance_id":13},{"manifest":"sleep.json","instance_id":7},{"manifest":"storage.json","key_value":[{"key":"alarm_occ","namespace":3,"access":"read-write"}]}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"test.audio","api":1,"instance_id":)")+(direction[0]=='r'?"13":"12")+R"(},{"capability":"test.sleep","api":1,"instance_id":7},{"capability":"test.alert","api":1,"instance_id":0}]}]})");
  save("i2s-trace.txt","");pid_t pid=fork();assert(pid>=0);if(!pid){execl(argv[0],argv[0],root.c_str(),mode.c_str(),(char*)nullptr);_exit(99);}int status=0;assert(waitpid(pid,&status,0)==pid);
  const auto output=read();if(!WIFEXITED(status) || WEXITSTATUS(status))fprintf(stderr,"%s failed:%s",mode.c_str(),output.c_str());assert(WIFEXITED(status) && !WEXITSTATUS(status));
  assert(output.find("APP loaded")!=std::string::npos && output.find("audio started")!=std::string::npos);
  for(const char* event:{"APP fini","APP unloaded","CHILD loaded","audio quiesced"})assert((output.find(event)==std::string::npos)==has("retained"));
  printf("Real Runtime/Graph/CpuPort/dlopen I2S + bound KV + Light/Deep lifecycle: %s PASS\n",mode.c_str());
 }
}
