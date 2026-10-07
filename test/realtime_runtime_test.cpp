#include "ports/esp32s3/CpuPort.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
static std::string root;static RiscCpu::Port* cpu;
static bool isOwner=true,safe=true;static int mode=0,backendFailure=0;
static risc_realtime_api_v1 savedRead{};static risc_realtime_control_api_v1 savedControl{};
static risc_realtime_snapshot_v1 timeValue{sizeof(timeValue),0,0,0,0,111,113};
extern "C" int test_time_mode(){return mode;}
extern "C" void test_time_owner(int v){isOwner=v;}
extern "C" void test_time_safe(int v){safe=v;}
extern "C" void test_time_backend(int v){backendFailure=v;}
extern "C" void test_time_save(const risc_realtime_api_v1* a,const risc_realtime_control_api_v1* b){savedRead=*a;savedControl=*b;}
extern "C" void test_time_old(){
 risc_realtime_snapshot_v1 out{sizeof(out)};
 if(savedRead.read)assert(savedRead.read(savedRead.context,&out)==RISC_REALTIME_CONTEXT);
 if(savedControl.seed)assert(savedControl.seed(savedControl.context,1,0)==RISC_REALTIME_CONTEXT);
}
extern "C" bool test_deep_timed(){return false;}
extern "C" void test_deep_trace(const char*){}
static bool owner(){return isOwner;}
static bool health(risc_runtime_health_v1*){return true;}
static bool log(const char*){return true;}
static uint64_t now(){return 0;}
static void waitMs(uint32_t){}
static bool open(uint8_t,bool,bool,bool){return true;}
static bool write(uint8_t,bool){return true;}
static bool read(uint8_t,bool* value){*value=mode!=2;return true;}
static bool pwm(uint8_t,uint32_t,uint16_t,uint16_t){return true;}
static bool close(uint8_t){return true;}
static bool iOpen(uint8_t,uint8_t,uint8_t,uint32_t){return true;}
static bool iTransfer(uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){return true;}
static bool sOpen(uint8_t,int16_t,int16_t,int16_t){return true;}
static bool sBegin(uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){return true;}
static bool sTransfer(uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){return true;}
static bool sEnd(uint8_t,uint8_t,uint32_t){return true;}
static bool valid(uint8_t pin){return pin<22;}
static bool ready(){return true;}
static bool arm(uint8_t pin,bool high,bool pullup){assert(pin==7 && !high && pullup);test_deep_trace("CPU armed");return true;}
static bool clear(uint8_t,bool){test_deep_trace("CPU cleanup");return true;}
static bool timerArm(uint32_t ms){assert(ms==123);test_deep_trace("CPU timer-armed");return true;}
static bool timerClear(){test_deep_trace("CPU timer-cleanup");return true;}
static bool hold(uint8_t pin,bool enable){assert(pin==6);(void)enable;test_deep_trace("CPU held");return true;}

static void enter(){
 risc_realtime_snapshot_v1 out{sizeof(out)};
 assert(savedRead.read(savedRead.context,&out)==RISC_REALTIME_CONTEXT);
 assert(savedControl.seed(savedControl.context,1,0)==RISC_REALTIME_CONTEXT);
} // Unexpected native return must retain/poison the port.
static int32_t timeRead(risc_realtime_snapshot_v1* out){
 assert(out && out->struct_size==sizeof(*out));*out=timeValue;
 switch(backendFailure){
 case 1:out->nanoseconds=1000000000;break;
 case 2:out->validity=2;break;
 case 3:out->reserved=1;break;
 case 4:out->monotonic_after_us=0;break;
 case 5:out->struct_size=0;break;
 case 6:out->epoch_seconds=-1;break;
 case 7:return RISC_REALTIME_IO;
 }
 return RISC_REALTIME_OK;
}
static int32_t timeSeed(int64_t seconds,uint32_t nanos){timeValue.validity=1;timeValue.epoch_seconds=seconds;timeValue.nanoseconds=nanos;return 0;}
static bool bind(RiscBoot::Runtime& r){return cpu->bind(r);}
static bool storageSafe(){return safe && cpu->providerStorageSafe();}
static bool appExitSafe(){return cpu->appExitSafe();}
static void file(const char* name,const char* contents){std::ofstream(root+"/"+name)<<contents;}
static int run(){
 RiscCpu::Hardware h{owner,now,waitMs,open,write,read,pwm,close,iOpen,iTransfer,close,sOpen,sBegin,sTransfer,sEnd,close};
 h.deepWakeValid=valid;h.deepReady=ready;h.deepWakeArm=arm;h.deepWakeClear=clear;h.deepSleep=enter;h.deepHold=hold;h.timerArm=timerArm;h.timerClear=timerClear;
 h.realtimeRead=timeRead;h.realtimeSeed=timeSeed;
 if(mode==4){h.realtimeRead=nullptr;h.realtimeSeed=nullptr;};
 RiscCpu::Port port(h);cpu=&port;
 RiscBoot::Runtime rt({owner,health,waitMs,log,bind,nullptr,appExitSafe,storageSafe});
 if(mode>=4 && mode<=6){assert(!rt.prepare(root.c_str()));return 0;}
 assert(rt.prepare(root.c_str()));
 assert(rt.run()==(mode!=3));test_time_old();
 if(mode==3)_exit(0); // Deliberately retained, no fake shutdown.
 return 0;
}
int main(int argc,char** argv){
 assert(argc==2 || argc==3);root=argv[1];if(argc==3){mode=atoi(argv[2]);int result=run();if(mode<2){timeValue={sizeof(timeValue),0,0,0,0,111,113};return run();}return result;}
 file("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[{"instance_id":7,"chip":{"vendor":"test","model":"gpio","revision":"unspecified"},"compatible":"test,gpio","config_type":"gpio.bank","config_version":1,"config":{"pins":[7,6],"active_high":true,"pull_up":true,"debounce_us":0,"long_press_us":0,"click_min_us":0}}]})");
 file("deep.json",R"({"type":"driver","id":"deep-probe","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"deep.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.gpio","api":1}],"provides":[{"capability":"test.deep","api":1}],"hardware_compatibility":[{"compatible":"test,gpio","revisions":["unspecified"],"config_type":"gpio.bank","config_version":1}]})");

 for(mode=0;mode<=7;++mode){
  const char* cap=mode==0?RISC_REALTIME_CAPABILITY:mode==6?"platform.realtime":RISC_REALTIME_CONTROL_CAPABILITY;
  std::string app=R"({"type":"application","id":"time-app","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"test.deep","api":1},{"capability":")";
  app+=cap;app+=R"(","api":1}]})";file("app.json",app.c_str());
  std::string boot=R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"deep.json","instance_id":7}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"test.deep","api":1,"instance_id":7},{"capability":")";
  boot+=cap;boot+=R"(","api":1,"instance_id":)";boot+=mode==5?"7":"0";boot+="}]}]}";file("boot.json",boot.c_str());
  if(mode==7){
   JsonDocument manifest,config;assert(RiscBoot::parse(app.data(),app.size(),manifest));assert(RiscBoot::parse(boot.data(),boot.size(),config));
   auto req=manifest["requires"].as<JsonArray>().add<JsonObject>();req["capability"]=RISC_REALTIME_CAPABILITY;req["api"]=1;
   auto grant=config["app_capabilities"][0]["grants"].as<JsonArray>().add<JsonObject>();grant["capability"]=RISC_REALTIME_CAPABILITY;grant["api"]=1;grant["instance_id"]=0;
   app.clear();boot.clear();serializeJson(manifest,app);serializeJson(config,boot);file("app.json",app.c_str());file("boot.json",boot.c_str());
  }
  const auto child=fork();assert(child>=0);if(!child){const auto arg=std::to_string(mode);execl(argv[0],argv[0],root.c_str(),arg.c_str(),static_cast<char*>(nullptr));_exit(99);}
  int status;assert(waitpid(child,&status,0)==child);if(status)fprintf(stderr,"realtime mode %d status %d\n",mode,status);assert(WIFEXITED(status) && WEXITSTATUS(status)==0);
 }
 puts("Runtime/CpuPort realtime: read/control separation, malformed/stale/owner rejection, refused and retained entry PASS");
}
