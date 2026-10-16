// Production main's exact handoff block, NativeBootstrap/Coordinator, NativeSntp,
// NativeRealtime and NativeRetainedWake. Only SDK/storage/network boundaries
// are modeled; no NVS storage implementation or target hardware is changed.
#include <sys/time.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <esp_system.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include "ports/esp32s3/NativeSntp.h"
#include "ports/esp32s3/NativeRetainedWake.h"
#include "ports/esp32s3/NativeBankStore.h"
#include "runtime/update/PairedBank.h"
#include "diagnostics/StageLog.h"
#include "diagnostics/Performance.h"
namespace RiscCpu::NativeRuntimeFiles {const RiscBoot::RuntimeFileSources* sources(){return nullptr;}}
static esp_reset_reason_t resetReason=ESP_RST_POWERON;
static int wakeReason=ESP_SLEEP_WAKEUP_TIMER;
static timeval sdkWall{};static uint64_t ticks=0;
static unsigned writes=0,starts=0,stops=0,inputReads=0,queueReads=0,releases=0;
static unsigned joins=0,downloads=0,aborts=0,availableChecks=0,installedRuns=0;
static bool radio=false,http=false,enabled=false,stopQueued=false,pending=false,ownerProfile=false,validRetention=true;
static std::string pendingBytes="unchanged queued request bytes",profileJson;
static Callback callback=nullptr;
esp_reset_reason_t esp_reset_reason(){return resetReason;}
int esp_sleep_get_wakeup_cause(){return wakeReason;}
int64_t esp_timer_get_time(){return ticks*1000;}
static bool owner(){return true;}
static int getWall(timeval* out,void*){*out=sdkWall;return 0;}
static int setWall(const timeval* in,const void*){sdkWall=*in;++writes;return 0;}
#define gettimeofday getWall
#define settimeofday setWall
#include "ports/esp32s3/NativeRealtime.cpp"
#undef gettimeofday
#undef settimeofday
bool esp_sntp_enabled(){return enabled;}
void esp_sntp_stop(){++stops;stopQueued=true;}
void esp_sntp_setservername(unsigned,const char*){if(stopQueued){enabled=false;stopQueued=false;}}
void esp_sntp_setoperatingmode(int){}
void esp_sntp_set_sync_mode(int mode){assert(mode==SNTP_SYNC_MODE_IMMED);}
void esp_sntp_set_sync_status(int){}
void esp_sntp_servermode_dhcp(bool active){assert(!active);}
void esp_sntp_set_time_sync_notification_cb(Callback value){callback=value;}
void esp_sntp_init(){
 enabled=true;++starts;
 // Confirmed in pinned liblwip.a sntp_sync_time: immediate settimeofday occurs
 // before the notification callback, even if a later download will fail.
 timeval received{1900000000,0};assert(setWall(&received,nullptr)==0);callback(&received);
}
namespace RiscDiagnostics {uint64_t monotonicUs(){return ticks*1000;}void timestamped(const char*,...) {}}
static void diagnosticFormat(const char*,...){}
static bool providerStorageSafe(){return true;}
static RiscProvision::InputStatus inputRead(void* context,const char* key,void* out,uint32_t cap,uint32_t* n){
 ++inputReads;*n=0;if(!context&&!ownerProfile)return RiscProvision::InputStatus::Missing;
 const std::string value=!std::strcmp(key,"descriptor")?R"({"schema":"riscrte.bootstrap","schema_version":2,"profile_key":"profile","time_key":"time"})":
  !std::strcmp(key,"time")?R"({"schema":"riscrte.sntp","schema_version":1,"servers":["time.cloudflare.com"]})":profileJson;
 *n=value.size();assert(*n<=cap);std::memcpy(out,value.data(),*n);return RiscProvision::InputStatus::Ready;
}
namespace RiscNvs {const RiscBoot::KeyValueBackend* backend(){return nullptr;}}
namespace RiscBootstrap {RiscProvision::Input nvsInput(){return {nullptr,inputRead};}}
namespace RiscProvisionRequest {
 RiscProvision::Input bootInput(RiscProvision::Input original,bool& preserving){
  ++queueReads;preserving=pending;return pending?RiscProvision::Input{&pendingBytes,inputRead}:original;
 }
 void releaseBootInput(){++releases;}
}
static const risc_http_client_v1 transport{1,sizeof(transport),nullptr,
 [](void*,const risc_http_request_v1* r,uint64_t* token)->int32_t{assert(radio&&!http&&r->utc_seconds==1900000000);http=true;*token=1;return RISC_HTTP_OK;},
 [](void*,uint64_t,void*,uint32_t,uint32_t* n)->int32_t{assert(http);++downloads;*n=0;return RISC_HTTP_TRANSPORT;},
 [](void*,uint64_t,risc_http_response_v1*)->int32_t{assert(false);return RISC_HTTP_TRANSPORT;},
 [](void*,uint64_t)->int32_t{assert(http);http=false;return RISC_HTTP_OK;}};
static RiscCpu::Hardware hardware(){
 RiscCpu::Hardware h{};h.owner=owner;h.now=[](){return ticks;};h.sleep=[](uint32_t ms){ticks+=ms;};
 h.radioJoin=[](const char*,const char*){++joins;radio=true;return true;};
 h.radioState=[](uint8_t* state,int8_t*){*state=2;return true;};
 h.radioAddresses=[](uint8_t* station,uint8_t*){station[0]=10;return true;};
 h.radioLeave=[](){radio=false;return true;};h.radioIdle=[](){return !radio;};
 h.httpClient=&transport;h.httpIdle=[](){return !http;};h.httpSafe=[](){return true;};return h;
}
static struct Cpu {RiscCpu::Hardware h=hardware();const RiscCpu::Hardware& bootstrapHardware(){return h;}} cpu;
namespace RiscBankStore {
 bool provisionAvailable(){++availableChecks;return true;}bool exitSafe(){return true;}
 const char* bootLabel(){return "bootfs0";}
 ProvisionIdentity provisionIdentity(const uint8_t (&)[32]){return ProvisionIdentity::Different;}
 ProvisionHistory provisionHistory(const uint8_t (&)[32]){return ProvisionHistory::Clear;}
 int32_t provisionBegin(const RiscProvision::Profile&,const uint8_t (&)[32],const RiscCpu::Hardware&,const RiscBoot::KeyValueBackend*,uint64_t* token,const RiscBoot::AppDataBackend*,bool){*token=1;return RISC_BANK_OK;}
 int32_t provisionStep(uint64_t,risc_bank_status_v1* out){out->state=RiscUpdate::Transaction::StoreStaging;return RISC_BANK_OK;}
 bool provisionStatus(uint64_t,risc_bank_status_v1*){assert(false);return false;}
 int32_t provisionWrite(uint64_t,size_t,const void*,uint32_t){assert(false);return RISC_BANK_IO;}
 int32_t provisionFinish(uint64_t){assert(false);return RISC_BANK_IO;}
 int32_t provisionActivate(uint64_t){assert(false);return RISC_BANK_IO;}
 int32_t provisionAbort(uint64_t){++aborts;return RISC_BANK_OK;}
 bool provisionRestart(uint64_t){assert(false);return false;}
}
static void bootHandoff(){
#if RISC_STAGE_LOGS
 uint64_t stageUs=0;
#endif
#include "boot_handoff.inc"
 ++installedRuns;
}
static void sleepTerminal(){throw 1;}
static void restoreBoot(bool deep){
 using namespace RiscCpu::NativeRealtime;
 if(deep){
  if(validRetention){assert(seed(1800000000,123000000)==RISC_REALTIME_OK);try{enter(sleepTerminal);}catch(int){}}
  sdkWall={1800000061,123000};
 }
 resetReason=deep?ESP_RST_DEEPSLEEP:ESP_RST_SW;ticks=0;
 RiscCpu::NativeRetainedWake::start();start();
}
static risc_realtime_snapshot_v1 snapshot(){risc_realtime_snapshot_v1 out{};out.struct_size=sizeof(out);assert(RiscCpu::NativeRealtime::read(&out)==0);return out;}
int main(int argc,char** argv){
 assert(argc==2);const std::string mode=argv[1];
 pending=mode.find("pending")!=std::string::npos;
 const bool deep=mode.find("deep")==0,subsequent=mode=="deep-pending-then-ordinary";
 ownerProfile=mode.find("owner")!=std::string::npos;
 validRetention=mode.find("unset")==std::string::npos;
 if(mode.find("gpio")!=std::string::npos)wakeReason=ESP_SLEEP_WAKEUP_EXT1;
 if(mode.find("other")!=std::string::npos)wakeReason=ESP_SLEEP_WAKEUP_UNDEFINED;
 profileJson=R"({"schema":"riscrte.provisioning","schema_version":1,"wifi":{"ssid":"test-network","password":"test-only-password"},"files":[)";
 for(const char* name:{"boot.json","board.json","default.elf"}){
  if(profileJson.back()!='[')profileJson+=',';
  profileJson+="{\"path\":\""+std::string(name)+"\",\"url\":\"https://example.test/"+name+"\",\"bytes\":1,\"sha256\":\""+std::string(64,'a')+"\"}";
 }profileJson+="]}";
 RiscCpu::NativeRealtime::configure(owner);RiscCpu::NativeRealtime::start();
 if(mode.find("poweron")==0){resetReason=ESP_RST_POWERON;RiscCpu::NativeRetainedWake::start();RiscCpu::NativeRealtime::start();}
 else restoreBoot(deep);
 const auto before=snapshot();const timeval wallBefore=sdkWall;const unsigned beforeWrites=writes;const auto bytesBefore=pendingBytes;
 assert(RiscCpu::NativeRetainedWake::coldBoot()==!deep);
 if(deep)assert(before.validity==(validRetention?RISC_REALTIME_VALID:RISC_REALTIME_UNSET));else assert(before.validity==RISC_REALTIME_UNSET);
 bootHandoff();assert(installedRuns==1 && pendingBytes==bytesBefore);
 const auto after=snapshot();
 if(RISC_NATIVE_PROVISION_REQUEST && deep){
  assert(!queueReads&&!releases&&!inputReads&&!availableChecks&&!starts&&!stops&&!joins&&!downloads&&!aborts);
  assert(writes==beforeWrites && sdkWall.tv_sec==wallBefore.tv_sec && sdkWall.tv_usec==wallBefore.tv_usec);
  assert(after.validity==before.validity&&after.epoch_seconds==before.epoch_seconds&&after.nanoseconds==before.nanoseconds);
 }else{
  assert(queueReads==unsigned(bool(RISC_NATIVE_PROVISION_REQUEST)) && releases==queueReads);
  const bool hasInput=ownerProfile||(RISC_NATIVE_PROVISION_REQUEST&&pending);
  if(hasInput){assert(starts==1 && writes==beforeWrites+1 && downloads==1 && aborts==1&&!enabled&&!radio&&!http);}
  else assert(!starts && !downloads && writes==beforeWrites);
  if(!deep)assert(after.validity==RISC_REALTIME_UNSET && !after.epoch_seconds);
 }
 if(subsequent){
  assert(RISC_NATIVE_PROVISION_REQUEST && !starts && !queueReads);
  resetReason=ESP_RST_SW;RiscCpu::NativeRealtime::start();assert(snapshot().validity==RISC_REALTIME_UNSET);
  bootHandoff();assert(installedRuns==2&&queueReads==1&&releases==1&&starts==1&&downloads==1&&aborts==1&&pendingBytes==bytesBefore);
  assert(snapshot().validity==RISC_REALTIME_UNSET);
 }
 std::printf("Production boot handoff: %s request=%d stage_logs=%d PASS\n",mode.c_str(),RISC_NATIVE_PROVISION_REQUEST,RISC_STAGE_LOGS);
}
