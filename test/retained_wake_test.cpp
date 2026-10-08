#include "ports/esp32s3/CpuPort.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
static std::string root,tracePath;static bool wake=false,timed=false;static RiscCpu::Port* cpu;
static bool isOwner=true,safe=true;static int mode=0;static uint32_t cause=0;
static RiscRetainedWake::Image image{};static RiscRetainedWake::Store* store;
static risc_retained_wake_api_v1 saved{};
extern "C" void test_wake_refused(){assert(!image.magic);store->commit();assert(image.magic && image.record.payload[0]==0);store->rollback();}
extern "C" int test_wake_mode(){return mode;}
extern "C" unsigned test_wake_cause(){return cause;}
extern "C" void test_wake_owner(int v){isOwner=v;}
extern "C" void test_wake_safe(int v){safe=v;}
extern "C" void test_wake_snapshot(const risc_retained_wake_api_v1* a){saved=*a;}
static bool storageSafe(){return safe && cpu->providerStorageSafe();}
extern "C" bool test_deep_timed(){return timed;}
extern "C" void test_deep_trace(const char* text){std::ofstream(tracePath,std::ios::app)<<text<<'\n';}
static bool owner(){return isOwner;}
static bool health(risc_runtime_health_v1* h){h->uptime_ms=wake?1:0;return true;}
static bool log(const char* s){test_deep_trace(s);return true;}
static uint64_t now(){return 0;}
static void waitMs(uint32_t){}
static bool open(uint8_t,bool,bool,bool){return true;}
static bool write(uint8_t,bool){return true;}
static bool read(uint8_t,bool* value){*value=mode!=3;return true;}
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
 store->commit();assert(image.magic);
 if(mode==4){store->rollback();return;}
 std::ofstream f(root+"/rtc.bin",std::ios::binary);f.write(reinterpret_cast<const char*>(&image),sizeof(image));f.close();_exit(73);
}
static bool bind(RiscBoot::Runtime& r){return cpu->bind(r);}
static bool appExitSafe(){return cpu->appExitSafe();}
static void file(const char* name,const char* contents){std::ofstream(root+"/"+name)<<contents;}
static void child(const char* exe,int m,unsigned c,int expected){
 pid_t pid=fork();assert(pid>=0);if(!pid){std::string a=std::to_string(m),b=std::to_string(c);execl(exe,exe,root.c_str(),a.c_str(),b.c_str(),static_cast<char*>(nullptr));_exit(99);}
 int status;assert(waitpid(pid,&status,0)==pid);if(!WIFEXITED(status) || WEXITSTATUS(status)!=expected)fprintf(stderr,"mode=%d cause=%u raw status=%d expected=%d\n",m,c,status,expected);assert(WIFEXITED(status) && WEXITSTATUS(status)==expected);
}
int main(int argc,char** argv){
 assert(argc==2 || argc==4);root=argv[1];tracePath=root+"/trace.txt";
 if(argc==4){
  mode=atoi(argv[2]);cause=unsigned(atoi(argv[3]));
  std::ifstream f(root+"/rtc.bin",std::ios::binary);if(f)f.read(reinterpret_cast<char*>(&image),sizeof(image));
  RiscRetainedWake::Store checkpoint(image);store=&checkpoint;store->boot(cause);assert(!image.magic);
  RiscCpu::Hardware h{owner,now,waitMs,open,write,read,pwm,close,iOpen,iTransfer,close,sOpen,sBegin,sTransfer,sEnd,close};
  h.deepWakeValid=valid;h.deepReady=ready;h.deepWakeArm=arm;h.deepWakeClear=clear;h.deepSleep=enter;h.deepHold=hold;h.timerArm=timerArm;h.timerClear=timerClear;
  RiscCpu::Port port(h);cpu=&port;RiscBoot::Port p{owner,health,waitMs,log,bind,nullptr,appExitSafe,storageSafe};p.retainedWake=store;
  static const RiscBoot::KeyValueBackend unusedStorage{nullptr,
   [](void*,uint32_t,const char*,void*,uint32_t,uint32_t*)->int32_t{assert(false);return -1;},
   [](void*,uint32_t,const char*,const void*,uint32_t)->int32_t{assert(false);return -1;}};
  p.keyValue=&unusedStorage;
  if(mode==5)p.retainedWake=nullptr;
  RiscBoot::Runtime runtime(p);
  if(mode==5 || mode==6){assert(!runtime.prepare(root.c_str()));return 0;}
  assert(runtime.prepare(root.c_str()));
  // Metadata-only admission sees the same capability without consuming boot RTC.
  {RiscBoot::Port metadata=p;
   // A separate CPU metadata binder is needed for scoped driver dependencies.
   RiscCpu::Port metadataCpu(h);cpu=&metadataCpu;
   metadata.bindPlatforms=bind;RiscBoot::Runtime admitted(metadata);
   assert(admitted.prepare(root.c_str()));cpu=&port;}
  bool result=runtime.run();assert(result==(mode!=4));assert(!image.magic);
  if(mode!=4){store->commit();assert(!image.magic);}
  assert(saved.clear(saved.context)==RISC_RETAINED_WAKE_CONTEXT);if(mode==4)_exit(0);return 0;
 }
 file("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[{"instance_id":7,"chip":{"vendor":"test","model":"gpio","revision":"unspecified"},"compatible":"test,gpio","config_type":"gpio.bank","config_version":1,"config":{"pins":[7,6],"active_high":true,"pull_up":true,"debounce_us":0,"long_press_us":0,"click_min_us":0}}]})");
 file("deep.json",R"({"type":"driver","id":"deep-probe","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"deep.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.gpio","api":1}],"provides":[{"capability":"test.deep","api":1}],"hardware_compatibility":[{"compatible":"test,gpio","revisions":["unspecified"],"config_type":"gpio.bank","config_version":1}]})");
 file("app.json",R"({"type":"application","id":"deep-app","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"test.deep","api":1},{"capability":"runtime.retained-wake","api":1}]})");
 file("boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"deep.json","instance_id":7}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"test.deep","api":1,"instance_id":7},{"capability":"runtime.retained-wake","api":1,"instance_id":0}]}]})");
 const char* cohort=R"({"schema":"riscrte.cohort","schema_version":1,"product":"test","version":"1.0.0","runtime_version":"0.1.46","source_repo":"example/test","source_revision":"1111111111111111111111111111111111111111","layout":"riscrte-paired-16m-v1","store_abi":1,"firmware_size":32,"firmware_sha256":"1111111111111111111111111111111111111111111111111111111111111111"})";
 if(RiscBoot::Runtime::MaxAppPolicyGrants==17){
  // Put retained-wake at row seventeen, with fifteen unused explicit KV
  // namespaces preceding the existing deep provider and retained authority.
  for(const char* name:{"app.json","boot.json"}){
   std::ifstream input(root+"/"+name);std::string json{std::istreambuf_iterator<char>(input),{}};
   std::string prefix;
   const bool manifest=!strcmp(name,"app.json");
   for(unsigned i=1;i<=(manifest?1u:15u);++i){
    prefix+="{\"capability\":\"storage.key-value\",\"api\":1";
    if(!manifest)prefix+=",\"instance_id\":"+std::to_string(i);
    prefix+="},";
   }
   const std::string key=manifest?"\"requires\":[":"\"grants\":[";
   json.insert(json.find(key)+key.size(),prefix);file(name,json.c_str());
  }
 }
 file("cohort.json",cohort);
 child(argv[0],5,RISC_BOOT_POWER_ON,0);
 file("cohort.json","{}");child(argv[0],6,RISC_BOOT_POWER_ON,0);file("cohort.json",cohort);
 child(argv[0],0,RISC_BOOT_POWER_ON,0);
 child(argv[0],2,RISC_BOOT_POWER_ON,73);
 for(unsigned c:{RISC_BOOT_DEEP_TIMER,RISC_BOOT_DEEP_GPIO,RISC_BOOT_DEEP_OTHER})child(argv[0],1,c,0);
 for(unsigned c:{RISC_BOOT_POWER_ON,RISC_BOOT_RESET})child(argv[0],0,c,0);
 // Foreign cohort and foreign app identity never receive an existing payload.
 std::string foreign=cohort;foreign.replace(foreign.find("example/test"),12,"example/else");file("cohort.json",foreign.c_str());child(argv[0],0,RISC_BOOT_DEEP_TIMER,0);file("cohort.json",cohort);
 std::ifstream appFile(root+"/app.json");std::string app{std::istreambuf_iterator<char>(appFile),{}};
 auto foreignApp=app;foreignApp.replace(foreignApp.find("deep-app"),8,"else-app");file("app.json",foreignApp.c_str());child(argv[0],0,RISC_BOOT_DEEP_TIMER,0);file("app.json",app.c_str());
 child(argv[0],3,RISC_BOOT_RESET,0);child(argv[0],4,RISC_BOOT_RESET,0);
 // Stale format and corruption independently reject, with unchanged outputs.
 std::ifstream input(root+"/rtc.bin",std::ios::binary);input.read(reinterpret_cast<char*>(&image),sizeof(image));const auto good=image;
 for(unsigned variant=0;variant<3;++variant){image=good;if(variant==0)image.format=2;else if(variant==1)image.record.payload[30]^=1;else image.magic=0;
  std::ofstream output(root+"/rtc.bin",std::ios::binary);output.write(reinterpret_cast<const char*>(&image),sizeof(image));output.close();child(argv[0],0,RISC_BOOT_DEEP_TIMER,0);}
 // The RTC envelope itself rejects every one-bit byte corruption and
 // boot consumes committed storage before any app can read it.
 RiscRetainedWake::Identity id{};risc_retained_wake_record_v1 record{sizeof(record),1,1,128,{}};
 RiscRetainedWake::Image memory{};RiscRetainedWake::Store staging(memory);staging.boot(RISC_BOOT_POWER_ON);staging.stage(id,record);
 assert(!memory.magic);staging.commit();const auto committed=memory;
 for(size_t offset=0;offset<sizeof(memory);++offset){memory=committed;reinterpret_cast<unsigned char*>(&memory)[offset]^=1;
  RiscRetainedWake::Store next(memory);next.boot(RISC_BOOT_DEEP_TIMER);uint32_t boot;auto result=record;
  assert(next.read(id,1,1,result,boot)==RISC_RETAINED_WAKE_ABSENT && !memory.magic);}
 memory=committed;RiscRetainedWake::Store first(memory);first.boot(RISC_BOOT_DEEP_GPIO);assert(!memory.magic);
 RiscRetainedWake::Store second(memory);second.boot(RISC_BOOT_DEEP_GPIO);uint32_t boot;auto result=record;
 assert(second.read(id,1,1,result,boot)==RISC_RETAINED_WAKE_ABSENT);
 assert(first.read(id,1,1,result,boot)==0 && first.read(id,1,1,result,boot)==RISC_RETAINED_WAKE_ABSENT);
 puts("Production Runtime/CPU/dlopen retained wake: terminal entry, fresh wake, cause, one-shot/type/schema, bounds/copy, owner/safety/revocation, refusal/return, power/reset/stale/corrupt/foreign cohort/app PASS");
}
