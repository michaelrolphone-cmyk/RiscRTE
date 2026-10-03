#include "ports/esp32s3/CpuPort.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
static std::string root,tracePath;static bool wake=false;static RiscCpu::Port* cpu;
extern "C" void test_deep_trace(const char* text){std::ofstream(tracePath,std::ios::app)<<text<<'\n';}
static bool owner(){return true;}
static bool health(risc_runtime_health_v1* h){h->uptime_ms=wake?1:0;return true;}
static bool log(const char* s){test_deep_trace(s);return true;}
static uint64_t now(){return 0;}
static void waitMs(uint32_t){}
static bool open(uint8_t,bool,bool,bool){return true;}
static bool write(uint8_t,bool){return true;}
static bool read(uint8_t,bool* value){*value=true;return true;}
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
static bool hold(uint8_t pin,bool enable){assert(pin==6 && enable);test_deep_trace("CPU held");return true;}
static void enter(){test_deep_trace("CPU terminal-entry");_exit(73);}
static bool bind(RiscBoot::Runtime& r){return cpu->bind(r);}
static void file(const char* name,const char* contents){std::ofstream(root+"/"+name)<<contents;}
static void runChild(const char* executable,const char* mode,int expected){
 pid_t child=fork();assert(child>=0);
 if(child==0){execl(executable,executable,root.c_str(),mode,static_cast<char*>(nullptr));_exit(99);}
 int status=0;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==expected);
}
int main(int argc,char** argv){
 assert(argc==2 || argc==3);root=argv[1];tracePath=root+"/deep-trace.txt";
 if(argc==3){
  wake=std::string(argv[2])=="wake";
  RiscCpu::Hardware h{owner,now,waitMs,open,write,read,pwm,close,iOpen,iTransfer,close,sOpen,sBegin,sTransfer,sEnd,close};
  h.deepWakeValid=valid;h.deepReady=ready;h.deepWakeArm=arm;h.deepWakeClear=clear;h.deepSleep=enter;h.deepHold=hold;
  RiscCpu::Port port(h);cpu=&port;RiscBoot::Runtime runtime({owner,health,waitMs,log,bind});
  assert(runtime.prepare(root.c_str()));
  if(!runtime.run()){fprintf(stderr,"Runtime failure: %s\n",runtime.error());return 98;}
  assert(wake && port.quiescent());return 0;
 }
 file("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[{"instance_id":7,"chip":{"vendor":"test","model":"gpio","revision":"unspecified"},"compatible":"test,gpio","config_type":"gpio.bank","config_version":1,"config":{"pins":[7,6],"active_high":true,"pull_up":true,"debounce_us":0,"long_press_us":0,"click_min_us":0}}]})");
 file("deep.json",R"({"type":"driver","id":"deep-probe","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"deep.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.gpio","api":1}],"provides":[{"capability":"test.deep","api":1}],"hardware_compatibility":[{"compatible":"test,gpio","revisions":["unspecified"],"config_type":"gpio.bank","config_version":1}]})");
 file("app.json",R"({"type":"application","id":"deep-app","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"test.deep","api":1}]})");
 file("boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"deep.json","instance_id":7}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"test.deep","api":1,"instance_id":7}]}]})");
 for(unsigned n=0;n<3;n++){
  file("deep-trace.txt","");runChild(argv[0],"cold",73);runChild(argv[0],"wake",0);
  std::ifstream f(tracePath);const std::string trace{std::istreambuf_iterator<char>(f),{}};
  const std::string expected="PROVIDER fresh\nAPP init\nAPP fresh cold\nCPU held\nCPU armed\nCPU terminal-entry\nPROVIDER fresh\nAPP init\nAPP fresh wake\nAPP fini\nPROVIDER quiesced\nPROVIDER stopped\n";
  if(trace!=expected)fprintf(stderr,"Unexpected trace:\n%s",trace.c_str());
  assert(trace==expected);
 }
 puts("Actual runtime + dynamic provider/app: three terminal deep entries and fresh-process wake boots, fresh BSS/grants, no old-stack resume or deep-entry teardown PASS");
}
