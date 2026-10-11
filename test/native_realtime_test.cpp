#include <initializer_list>
#include <fstream>
#include <string>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>

#include <sys/time.h>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <esp_system.h>
static esp_reset_reason_t reset=ESP_RST_POWERON;
esp_reset_reason_t esp_reset_reason(){return reset;}
static bool own=true,failSet=false,failRead=false;
static int64_t monotonic=0;static timeval wall{};static unsigned sets=0,wallReads=0;
int64_t esp_timer_get_time(){return monotonic++;}
static int readTime(timeval* v,void*){++wallReads;*v=wall;return failRead?-1:0;}
static int setTime(const timeval* v,const void*){++sets;wall=*v;return failSet?-1:0;}
#define gettimeofday readTime
#define settimeofday setTime
// Exercise the production adapter and inspect only its RTC validity image.
#include "ports/esp32s3/NativeRealtime.cpp"
#undef gettimeofday
#undef settimeofday
static bool testOwner(){return own;}
static void restart(){throw 1;}
static std::string rtcFile;
static void saveRestart(){
 using namespace RiscCpu::NativeRealtime;
 std::ofstream f(rtcFile,std::ios::binary);f.write(reinterpret_cast<const char*>(&stamp),sizeof(stamp));
 assert(f.good());f.close();throw 1;
}
int main(int argc,char** argv){
 using namespace RiscCpu::NativeRealtime;
 configure(testOwner);risc_realtime_snapshot_v1 s{};s.struct_size=sizeof(s);
 if(argc==3){
  std::ifstream f(argv[1],std::ios::binary);f.read(reinterpret_cast<char*>(&stamp),sizeof(stamp));assert(f.good());
  reset=static_cast<esp_reset_reason_t>(atoi(argv[2]));wall={1800000061,17000};
  assert(read(&s)==RISC_REALTIME_CONTEXT);start();assert(read(&s)==0 && s.monotonic_before_us==0);
  assert(s.validity==(reset==ESP_RST_DEEPSLEEP?1u:0u));
  if(s.validity)assert(s.epoch_seconds==1800000061 && s.nanoseconds==17000000);
  else assert(!s.epoch_seconds && !s.nanoseconds);
  return 0;
 }
 assert(argc==2);rtcFile=argv[1];
 assert(read(&s)==RISC_REALTIME_CONTEXT);start();
 wall.tv_sec=1800000000; // A plausible raw SDK value never grants validity.
 assert(read(&s)==0 && !s.validity && !s.epoch_seconds && !s.nanoseconds);
 for(auto reason:{ESP_RST_UNKNOWN,ESP_RST_POWERON,ESP_RST_EXT,ESP_RST_SW,ESP_RST_PANIC,ESP_RST_INT_WDT,ESP_RST_TASK_WDT,ESP_RST_WDT,ESP_RST_BROWNOUT,ESP_RST_SDIO,ESP_RST_DEEPSLEEP}){
  reset=reason;start();assert(read(&s)==0 && !s.validity);
 }
 // An untrusted SDK wall value cannot prevent explicit recovery from an
 // external RTC. Cold/reset UNSET is independent of SDK wall corruption/I-O.
 const auto unseededReads=wallReads;
 for(auto reason:{ESP_RST_POWERON,ESP_RST_SW,ESP_RST_DEEPSLEEP}) {
  reset=reason;start();
  for(timeval raw: {timeval{-1,0},timeval{INT64_C(2147483648),0},
                    timeval{1,-1},timeval{1,1000000}}) {
   wall=raw;
   for(bool failure:{false,true}) {
    failRead=failure;s={};s.struct_size=sizeof(s);
    assert(read(&s)==RISC_REALTIME_OK && s.validity==RISC_REALTIME_UNSET);
    assert(!s.epoch_seconds && !s.nanoseconds && s.monotonic_after_us>=s.monotonic_before_us);
   }
  }
 }
 assert(wallReads==unseededReads);failRead=false;wall={1800000000,0};
 auto invalidMono=s;monotonic=-1;
 assert(read(&s)==RISC_REALTIME_IO && !memcmp(&s,&invalidMono,sizeof(s)));monotonic=0;
 assert(seed(-1,0)==RISC_REALTIME_INVALID);
 assert(seed(INT64_MAX,0)==RISC_REALTIME_INVALID);
 assert(seed(1,1000000000)==RISC_REALTIME_INVALID);
 assert(seed(1,1)==RISC_REALTIME_INVALID && !sets);
 own=false;assert(seed(1,0)==RISC_REALTIME_CONTEXT && read(&s)==RISC_REALTIME_CONTEXT);own=true;
 assert(seed(1800000000,123456000)==0 && sets==1);
 assert(read(&s)==0 && s.validity==1 && s.epoch_seconds==1800000000 && s.nanoseconds==123456000);
 assert(s.monotonic_after_us==s.monotonic_before_us+1);
 auto before=s;assert(read(nullptr)==RISC_REALTIME_INVALID);s.struct_size--;before=s;
 assert(read(&s)==RISC_REALTIME_INVALID && !memcmp(&s,&before,sizeof(s)));s.struct_size=sizeof(s);
 before=s;failRead=true;assert(read(&s)==RISC_REALTIME_IO && !memcmp(&s,&before,sizeof(s)));failRead=false;
 for(auto reason:{ESP_RST_UNKNOWN,ESP_RST_POWERON,ESP_RST_EXT,ESP_RST_SW,ESP_RST_PANIC,ESP_RST_INT_WDT,ESP_RST_TASK_WDT,ESP_RST_WDT,ESP_RST_BROWNOUT,ESP_RST_SDIO,ESP_RST_DEEPSLEEP}){
  assert(seed(1800000000,999999000)==0);
  try{enter(restart);}catch(int){}
  wall.tv_sec+=61;wall.tv_usec=17000;monotonic=0;reset=reason;start();
  assert(read(&s)==0 && s.monotonic_before_us==0);
  assert(s.validity==(reason==ESP_RST_DEEPSLEEP?1u:0u));
  if(s.validity)assert(s.epoch_seconds==1800000061 && s.nanoseconds==17000000); // SDK advanced; no double add.
 }
 assert(seed(1800000000,123456000)==0);try{enter(saveRestart);}catch(int){}
 for(auto reason:{ESP_RST_POWERON,ESP_RST_SW,ESP_RST_DEEPSLEEP}){
  const auto pid=fork();assert(pid>=0);
  if(!pid){const auto arg=std::to_string(reason);execl(argv[0],argv[0],rtcFile.c_str(),arg.c_str(),static_cast<char*>(nullptr));_exit(99);}
  int status=0;assert(waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0);
 }
 assert(seed(0,0)==0);assert(read(&s)==0 && s.validity && !s.epoch_seconds);
 assert(seed(2,0)==0);enter([](){}); // Returning deep backend cannot retain validity.
 assert(read(&s)==0 && s.validity);reset=ESP_RST_DEEPSLEEP;start();assert(read(&s)==0 && !s.validity);
 for(unsigned bit=0;bit<64;++bit){
  assert(seed(3,0)==0);try{enter(restart);}catch(int){}
  reinterpret_cast<unsigned char*>(&stamp)[bit/8]^=1u<<(bit%8);
  start();assert(read(&s)==0 && !s.validity);
 }
 assert(seed(INT32_MAX,999999000)==0);assert(read(&s)==0);
 failSet=true;assert(seed(4,0)==RISC_REALTIME_IO);failSet=false;
 assert(read(&s)==0 && !s.validity);
 assert(seed(1800000000,0)==RISC_REALTIME_OK);
 for(timeval raw: {timeval{-1,0},timeval{INT64_C(2147483648),0},
                   timeval{1,-1},timeval{1,1000000}}) {
  wall=raw;before=s;assert(read(&s)==RISC_REALTIME_IO && !memcmp(&s,&before,sizeof(s)));
 }
 puts("Native realtime: explicit seed, cold/reset invalidity, SDK deep-time advance, rollback, corrupt retention and failures PASS");
}
