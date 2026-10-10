#pragma once
/* Generic station-only IDF 4.4 adapter. Calls are serialized by the CPU port.
 * Exclusive radio/default-event-loop owner; never share with Arduino WiFi.
 * No app strings or code pointers are retained. See native_radio_shim/README.md
 * for the pinned SDK cleanup contracts and limits exercised by the host shim.
 * RF scans never block for completion, but synchronous SDK control/cleanup
 * calls have no cancellation API or enforceable 100-ms wall-clock bound. */
#include <RiscRadioScanV1.h>
#include "diagnostics/StageLog.h"
#include <esp_event.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_wifi_default.h>
#include <esp_timer.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
namespace RiscCpu { namespace NativeRadio {
// Firmware-only worker hooks. Configured once before admitting asynchronous
// work; never contain application/provider functions or borrowed storage.
static std::atomic<bool> cancelRequested{false};
static bool asyncExecuting=false; // worker-owned while asynchronous work exists
using StageSink=void (*)(uint32_t,int32_t,bool);
static StageSink stageSink=nullptr;
enum Stage : uint32_t { Probe=1, NetifInit, LoopCreate, NetifNew, Attach,
  Defaults, Register, Init, Storage, Mode, Config, Start, Connect, ApInfo,
  NetifUp, IpInfo, ScanStart, Records, ScanStop, ListClear, Disconnect,
  ConfigClear, Stop, Deinit, Unregister, DriverClear, DriverConfigClear,
  LoopDelete, DhcpStop, NetifStop, NetifDestroy, LogSet, LogGet };
template<class F> inline auto sdkCall(Stage stage,F function)->decltype(function()){
  if(asyncExecuting && stageSink)stageSink(stage,0,true);
  auto result=function();
  if(asyncExecuting && stageSink)stageSink(stage,int32_t(result),false);
  return result;
}
template<class F> inline void sdkVoid(Stage stage,F function){
  if(asyncExecuting && stageSink)stageSink(stage,0,true);
  function();
  if(asyncExecuting && stageSink)stageSink(stage,0,false);
}
#define RADIO_STAGE_LOG(...) do { if(!asyncExecuting) RISC_STAGE_LOG(__VA_ARGS__); } while(0)
inline bool cancelled(){return asyncExecuting && cancelRequested.load(std::memory_order_acquire);}

constexpr int64_t ScanTimeoutUs=INT64_C(10000000);
constexpr int64_t JoinTimeoutUs=INT64_C(30000000);
enum Operation : uint32_t { None=0, Join=1, Scan=2 };
struct State {
  bool loop=false,wifi=false,startAttempted=false,connectAttempted=false;
  bool attachAttempted=false,driverClearPending=false,closing=false;
  bool scanAttempted=false,scanList=false,credentials=false,established=false;
  esp_netif_t* netif=nullptr;
  esp_event_handler_instance_t handler=nullptr;
  Operation operation=None;
  uint8_t savedLogCount=0;
  esp_log_level_t savedLogLevels[4]{};
  int64_t scanDeadline=0,joinDeadline=0;
  // One bounded scan-session snapshot; no permanent 600-byte DRAM cache.
  // Native callbacks never touch this allocation, and never retain its address.
  garden_radio_scan_result_v1* result=nullptr;
#if RISC_STAGE_LOGS
  uint8_t stageLink=255;
  bool stageFailureReported=false;
#endif
};
static State s;
inline void memoryStage(const char* stage){
  (void)stage;
#if RISC_STAGE_LOGS
  // Snapshot the two allocation pools separately. ELF mappings use PSRAM;
  // Wi-Fi SDK control resources can still exhaust/fragment internal memory.
  // This adds no allocation, retry, ownership change or credential output.
  constexpr uint32_t internal=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
  constexpr uint32_t external=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
  RADIO_STAGE_LOG("radio wifi memory stage=%s internal_free=%lu internal_largest=%lu psram_free=%lu psram_largest=%lu dma_largest=%lu",stage,
    static_cast<unsigned long>(heap_caps_get_free_size(internal)),
    static_cast<unsigned long>(heap_caps_get_largest_free_block(internal)),
    static_cast<unsigned long>(heap_caps_get_free_size(external)),
    static_cast<unsigned long>(heap_caps_get_largest_free_block(external)),
    static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL)));
#endif
}
inline bool sdkFailure(const char* step,esp_err_t code){
  (void)step;(void)code;
  RADIO_STAGE_LOG("radio wifi failure step=%s code=%d",step,int(code));memoryStage(step);return false;
}
inline bool sdk(const char* step,esp_err_t code){return code==ESP_OK || sdkFailure(step,code);}
inline void linkStage(uint8_t state){
  (void)state;
#if RISC_STAGE_LOGS
  if(s.stageLink!=state){s.stageLink=state;RADIO_STAGE_LOG("radio wifi link state=%s",state==2?"up":state==1?"joining":"down");}
#endif
}
static const char* const LogTags[]={"wifi","wifi_init","wifi_init_default","wifi_netif"};
inline bool suppressLogs(){
  // IDF's void setter can silently fail allocation. Verify every override
  // before any SDK initialization/configuration receives the station SSID.
  for(uint8_t i=0;i<4;++i){
    if(cancelled())return false;
    s.savedLogLevels[i]=sdkCall(LogGet,[&](){return esp_log_level_get(LogTags[i]);});s.savedLogCount=i+1;
    if(cancelled())return false;
    sdkVoid(LogSet,[&](){esp_log_level_set(LogTags[i],ESP_LOG_NONE);});
    if(cancelled())return false;
    if(sdkCall(LogGet,[&](){return esp_log_level_get(LogTags[i]);})!=ESP_LOG_NONE){RADIO_STAGE_LOG("radio wifi failure step=suppress-logs tag=%u",unsigned(i));return false;}
  }
  return true;
}
inline bool ensureLogsSuppressed(){
  if(s.savedLogCount!=4 || (cancelled() && !s.closing))return false;
  for(const auto* tag:LogTags){
    if(cancelled() && !s.closing)return false;
    if(sdkCall(LogGet,[&](){return esp_log_level_get(tag);})!=ESP_LOG_NONE)sdkVoid(LogSet,[&](){esp_log_level_set(tag,ESP_LOG_NONE);});
    if(sdkCall(LogGet,[&](){return esp_log_level_get(tag);})!=ESP_LOG_NONE){RADIO_STAGE_LOG("radio wifi failure step=keep-logs-suppressed");return false;}
  }
  return true;
}
inline bool restoreLogs(){
  // Called only after all radio/netif/hooks/event resources are gone. Retain a
  // failed restoration for retry, without restoring active-radio diagnostics.
  while(s.savedLogCount){
    const uint8_t i=s.savedLogCount-1;
    sdkVoid(LogSet,[&](){esp_log_level_set(LogTags[i],s.savedLogLevels[i]);});
    if(sdkCall(LogGet,[&](){return esp_log_level_get(LogTags[i]);})!=s.savedLogLevels[i]){RADIO_STAGE_LOG("radio wifi failure step=restore-logs tag=%u",unsigned(i));return false;}
    --s.savedLogCount;
  }
  return true;
}
// TCP/IP infrastructure cannot be deinitialized in IDF 4.4. It owns no radio,
// station interface or app callback after leave; keep its one-time init flag.
static bool netifInitialized=false;
static uint32_t nextGeneration=0;
static std::atomic<uint32_t> generation{0},eventOperation{None},scanEvent{0},joinFailed{0};
#if RISC_STAGE_LOGS
static std::atomic<uint32_t> disconnectReason{0},scanStatus{0};
#endif
inline void wipe(void* data,size_t count){auto* p=static_cast<volatile uint8_t*>(data);while(count--)*p++=0;}
inline size_t boundedLength(const char* value,size_t maximum){
  if(!value)return maximum+1;
  size_t n=0;while(n<=maximum && value[n])++n;return n;
}
inline void event(void* argument,esp_event_base_t base,int32_t id,void* data){
  const uint32_t token=uint32_t(reinterpret_cast<uintptr_t>(argument));
  if(cancelRequested.load(std::memory_order_acquire) || !token || token!=generation.load(std::memory_order_acquire) || base!=WIFI_EVENT)return;
  const auto operation=eventOperation.load(std::memory_order_acquire);
  if(operation==Join && id==WIFI_EVENT_STA_DISCONNECTED){
#if RISC_STAGE_LOGS
    // Save only the numeric SDK reason, never SSID/BSSID from the event.
    disconnectReason.store(data?static_cast<const wifi_event_sta_disconnected_t*>(data)->reason:0,std::memory_order_relaxed);
#endif
    joinFailed.store(1,std::memory_order_release);
  }
  if(operation==Scan && id==WIFI_EVENT_SCAN_DONE && data){
    const auto* done=static_cast<const wifi_event_sta_scan_done_t*>(data);
#if RISC_STAGE_LOGS
    scanStatus.store(done->status,std::memory_order_relaxed);
#endif
    scanEvent.store(done->status?2:1,std::memory_order_release);
  }
  // Never reconnect, copy credentials, call SDK functions or touch s here.
}
inline bool idle(){return !s.loop && !s.wifi && !s.netif && !s.handler && !s.closing && !s.savedLogCount && !s.result;}
inline bool wifiAbsent(esp_err_t result){return result==ESP_OK || result==ESP_ERR_WIFI_NOT_INIT || result==ESP_ERR_WIFI_NOT_STARTED;}
inline bool leave(){
  const bool active=!idle();(void)active;
  if(active)RADIO_STAGE_LOG("radio wifi cleanup begin");
  // Revoke event authority before touching the SDK. A failed cleanup remains
  // closed and blocks all new starts until a later successful leave.
  generation.store(0,std::memory_order_release);eventOperation.store(None,std::memory_order_release);
  s.closing=true;
  if(s.wifi && !ensureLogsSuppressed())return false;
  if(s.scanAttempted){
    const esp_err_t result=sdkCall(ScanStop,[&](){return esp_wifi_scan_stop();});if(!wifiAbsent(result))return sdkFailure("scan-stop",result);
    s.scanAttempted=false;
  }
  if(s.scanList){
    const esp_err_t result=sdkCall(ListClear,[&](){return esp_wifi_clear_ap_list();});if(!wifiAbsent(result))return sdkFailure("scan-list-clear",result);
    s.scanList=false;
  }
  if(s.connectAttempted){
    const esp_err_t result=sdkCall(Disconnect,[&](){return esp_wifi_disconnect();});
    if(!wifiAbsent(result) && result!=ESP_ERR_WIFI_NOT_CONNECT)return sdkFailure("disconnect",result);
    s.connectAttempted=false;
  }
  if(s.credentials){
    wifi_config_t empty{};
    // The only SDK credential copy is RAM-only; overwrite before stop/deinit.
    if(!sdk("config-clear",sdkCall(ConfigClear,[&](){return esp_wifi_set_config(WIFI_IF_STA,&empty);})))return false;
    s.credentials=false;
  }
  if(s.startAttempted){
    const esp_err_t result=sdkCall(Stop,[&](){return esp_wifi_stop();});if(!wifiAbsent(result))return sdkFailure("stop",result);
    s.startAttempted=false;
  }
  if(s.wifi){
    const esp_err_t result=sdkCall(Deinit,[&](){return esp_wifi_deinit();});
    if(result!=ESP_OK && result!=ESP_ERR_WIFI_NOT_INIT)return sdkFailure("deinit",result);
    s.wifi=false;
  }
  if(s.handler){
    if(!sdk("event-unregister",sdkCall(Unregister,[&](){return esp_event_handler_instance_unregister(WIFI_EVENT,ESP_EVENT_ANY_ID,s.handler);})))return false;
    s.handler=nullptr;
  }
  if(s.attachAttempted){
    // IDF4.4 clears default registrations and destroys its driver even when
    // set_driver_config fails. Never call that destructive helper twice.
    const esp_err_t result=sdkCall(DriverClear,[&](){return esp_wifi_clear_default_wifi_driver_and_handlers(s.netif);});
    s.attachAttempted=false;
    if(result!=ESP_OK){s.driverClearPending=true;return sdkFailure("default-driver-clear",result);}
  }
  if(s.driverClearPending){
    esp_netif_driver_ifconfig_t empty{};
    if(!sdk("driver-config-clear",sdkCall(DriverConfigClear,[&](){return esp_netif_set_driver_config(s.netif,&empty);})))return false;
    s.driverClearPending=false;
  }
  // Delete the owned event task/queue (including undelivered old SDK events)
  // before destroying the netif or allowing a new attempt. Generation tagging
  // alone cannot reject old queued events dispatched to a NEW registration.
  if(s.loop){
    if(!sdk("event-loop-delete",sdkCall(LoopDelete,[&](){return esp_event_loop_delete_default();})))return false;
    s.loop=false;
  }
  if(s.netif){
    const esp_err_t result=sdkCall(DhcpStop,[&](){return esp_netif_dhcpc_stop(s.netif);});
    if(result!=ESP_OK && result!=ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED && result!=ESP_ERR_ESP_NETIF_IF_NOT_READY)return sdkFailure("dhcp-stop",result);
    sdkVoid(NetifStop,[&](){esp_netif_action_stop(s.netif,WIFI_EVENT,WIFI_EVENT_STA_STOP,nullptr);});
    sdkVoid(NetifDestroy,[&](){esp_netif_destroy(s.netif);});s.netif=nullptr;
  }
  if(!restoreLogs())return false;
  if(s.result){wipe(s.result,sizeof(*s.result));heap_caps_free(s.result);s.result=nullptr;}
  s={};scanEvent.store(0,std::memory_order_release);joinFailed.store(0,std::memory_order_release);
  if(active)RADIO_STAGE_LOG("radio wifi cleanup result=ok state=idle");
  return true;
}
inline bool continueSetup(){return !cancelled();}
inline bool begin(Operation operation){
  if(!idle() || !continueSetup() || nextGeneration==std::numeric_limits<uint32_t>::max())return false;
  RADIO_STAGE_LOG("radio wifi begin operation=%s",operation==Join?"connect":"scan");
  wifi_mode_t existingMode{};
  const esp_err_t existing=sdkCall(Probe,[&](){return esp_wifi_get_mode(&existingMode);});
  if(existing!=ESP_ERR_WIFI_NOT_INIT)return sdkFailure("exclusive-owner-check",existing);
  if(!continueSetup())return false;
  s.operation=operation;
  if(!netifInitialized){
    if(!sdk("netif-init",sdkCall(NetifInit,[](){return esp_netif_init();}))){s={};return false;}
    netifInitialized=true;if(!continueSetup())return false;
  }
  if(!sdk("event-loop-create",sdkCall(LoopCreate,[](){return esp_event_loop_create_default();}))){s={};return false;}
  s.loop=true;if(!continueSetup())return false;
  if(!suppressLogs() || !continueSetup()){leave();return false;}
  esp_netif_config_t netifConfig=ESP_NETIF_DEFAULT_WIFI_STA();
  if(asyncExecuting && stageSink)stageSink(NetifNew,0,true);
  s.netif=esp_netif_new(&netifConfig);
  if(asyncExecuting && stageSink)stageSink(NetifNew,s.netif?0:-1,false);
  if(!s.netif){leave();return false;}
  if(!continueSetup())return false;
  s.attachAttempted=true;
  if(!sdk("netif-attach",sdkCall(Attach,[&](){return esp_netif_attach_wifi_station(s.netif);})) || !continueSetup()){leave();return false;}
  if(!sdk("default-handlers",sdkCall(Defaults,[](){return esp_wifi_set_default_wifi_sta_handlers();})) || !continueSetup()){leave();return false;}
  const uint32_t token=++nextGeneration;
  generation.store(token,std::memory_order_release);eventOperation.store(operation,std::memory_order_release);
  scanEvent.store(0,std::memory_order_release);joinFailed.store(0,std::memory_order_release);
#if RISC_STAGE_LOGS
  disconnectReason.store(0,std::memory_order_relaxed);scanStatus.store(0,std::memory_order_relaxed);
#endif
  if(!sdk("event-register",sdkCall(Register,[&](){return esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event,reinterpret_cast<void*>(uintptr_t(token)),&s.handler);})) || !continueSetup()){leave();return false;}
  wifi_init_config_t config=WIFI_INIT_CONFIG_DEFAULT();config.nvs_enable=false;
  memoryStage("before-sdk-init");
  if(!sdk("init",sdkCall(Init,[&](){return esp_wifi_init(&config);} ))){leave();return false;}
  s.wifi=true;memoryStage("sdk-init-complete");if(!continueSetup())return false;
  if(!sdk("storage-ram",sdkCall(Storage,[](){return esp_wifi_set_storage(WIFI_STORAGE_RAM);})) || !continueSetup()){leave();return false;}
  if(!sdk("station-mode",sdkCall(Mode,[](){return esp_wifi_set_mode(WIFI_MODE_STA);})) || !continueSetup()){leave();return false;}
  RADIO_STAGE_LOG("radio wifi begin result=ready operation=%s",operation==Join?"connect":"scan");return true;
}
inline bool join(const char* ssid,const char* password){
  const size_t ssidLength=boundedLength(ssid,32),passwordLength=boundedLength(password,63);
  if(!ssidLength || ssidLength>32 || passwordLength>63 || (passwordLength && passwordLength<8) || !idle())return false;
  // Copy before any SDK/task activity. No borrowed caller buffer survives.
  wifi_config_t config{};std::memcpy(config.sta.ssid,ssid,ssidLength);
  if(passwordLength)std::memcpy(config.sta.password,password,passwordLength);
  config.sta.threshold.authmode=passwordLength?WIFI_AUTH_WPA_PSK:WIFI_AUTH_OPEN;
  config.sta.pmf_cfg.capable=true;config.sta.pmf_cfg.required=false;
  if(!begin(Join)){wipe(&config,sizeof(config));return false;}
  if(!ensureLogsSuppressed() || !continueSetup()){wipe(&config,sizeof(config));leave();return false;}
  s.credentials=true;
  const esp_err_t configured=sdkCall(Config,[&](){return esp_wifi_set_config(WIFI_IF_STA,&config);});
  wipe(&config,sizeof(config));
  if(!sdk("station-config",configured) || !continueSetup()){leave();return false;}
  if(!ensureLogsSuppressed() || !continueSetup()){leave();return false;}
  s.startAttempted=true;
  memoryStage("before-sdk-start");
  if(!sdk("start",sdkCall(Start,[&](){return esp_wifi_start();})) || !continueSetup()){leave();return false;}
  memoryStage("sdk-start-complete");
  if(!ensureLogsSuppressed() || !continueSetup()){leave();return false;}
  s.connectAttempted=true;s.joinDeadline=esp_timer_get_time()+JoinTimeoutUs;
  if(!sdk("connect",sdkCall(Connect,[&](){return esp_wifi_connect();})) || !continueSetup()){leave();return false;}
  RADIO_STAGE_LOG("radio wifi connect result=accepted");
  return true;
}
inline bool state(uint8_t* status,int8_t* rssi){
  if(!status || !rssi)return false;
  *status=0;*rssi=0;
  if(s.closing)return false;
  if(s.operation!=Join || !s.wifi || !s.startAttempted)return true;
  if(joinFailed.load(std::memory_order_acquire)){
#if RISC_STAGE_LOGS
    if(!s.stageFailureReported){s.stageFailureReported=true;RADIO_STAGE_LOG("radio wifi disconnect-event reason=%lu",static_cast<unsigned long>(disconnectReason.load(std::memory_order_relaxed)));}
#endif
    linkStage(0);return true;
  }
  wifi_ap_record_t ap{};struct ApWipe{wifi_ap_record_t& value;~ApWipe(){wipe(&value,sizeof(value));}} apWipe{ap};esp_netif_ip_info_t ip{};
  if(sdkCall(ApInfo,[&](){return esp_wifi_sta_get_ap_info(&ap);})==ESP_OK && sdkCall(NetifUp,[&](){return esp_netif_is_netif_up(s.netif);}) &&
     sdkCall(IpInfo,[&](){return esp_netif_get_ip_info(s.netif,&ip);})==ESP_OK && ip.ip.addr && !joinFailed.load(std::memory_order_acquire)){
    s.established=true;*status=2;*rssi=ap.rssi;linkStage(2);return true;
  }
  if(joinFailed.load(std::memory_order_acquire) || s.established){linkStage(0);return true;}
  // Poll-driven deadline, with no background retry. Stop before returning DOWN
  // for a timed-out join, so a late SDK association cannot revive that attempt.
  if(esp_timer_get_time()>=s.joinDeadline){RADIO_STAGE_LOG("radio wifi connect result=timeout");return leave();}
  *status=1;linkStage(1);return true;
}
inline bool addresses(uint8_t station[12],uint8_t ap[12]){
  if(!station || !ap)return false;
  std::memset(station,0,12);std::memset(ap,0,12);
  uint8_t status=0;int8_t rssi=0;if(!state(&status,&rssi))return false;
  if(status!=2)return true;
  esp_netif_ip_info_t ip{};if(!sdk("ip-info",sdkCall(IpInfo,[&](){return esp_netif_get_ip_info(s.netif,&ip);})))return false;
  // esp_ip4_addr_t.addr already stores the network byte order octets.
  std::memcpy(station,&ip.ip.addr,4);std::memcpy(station+4,&ip.gw.addr,4);std::memcpy(station+8,&ip.netmask.addr,4);
  return true;
}
inline uint8_t auth(wifi_auth_mode_t mode){
  switch(mode){
    case WIFI_AUTH_OPEN:return GARDEN_RADIO_AUTH_OPEN;
    case WIFI_AUTH_WPA_PSK:return GARDEN_RADIO_AUTH_WPA_PSK;
    case WIFI_AUTH_WPA2_PSK:return GARDEN_RADIO_AUTH_WPA2_PSK;
    case WIFI_AUTH_WPA_WPA2_PSK:return GARDEN_RADIO_AUTH_WPA_WPA2_PSK;
    case WIFI_AUTH_WPA3_PSK:return GARDEN_RADIO_AUTH_WPA3_PSK;
    case WIFI_AUTH_WPA2_WPA3_PSK:return GARDEN_RADIO_AUTH_WPA2_WPA3_PSK;
    default:return GARDEN_RADIO_AUTH_UNSUPPORTED;
  }
}
inline bool scanStart(){
  if(!idle())return false;
  // Allocate a single fixed-size snapshot before any SDK setup. This is native
  // session memory, not app-ledger memory; app return cannot reclaim it while
  // RF/cleanup still owns it. A failed begin has not published this pointer.
  auto* result=static_cast<garden_radio_scan_result_v1*>(
    heap_caps_calloc(1,sizeof(garden_radio_scan_result_v1),MALLOC_CAP_8BIT));
  if(!result){RADIO_STAGE_LOG("radio wifi scan result=failed step=result-buffer reason=out-of-memory");memoryStage("scan-buffer-failed");return false;}
  if(!begin(Scan)){wipe(result,sizeof(*result));heap_caps_free(result);return false;}
  s.result=result;
  if(!ensureLogsSuppressed() || !continueSetup()){leave();return false;}
  s.startAttempted=true;
  memoryStage("before-sdk-start");
  if(!sdk("start",sdkCall(Start,[&](){return esp_wifi_start();})) || !continueSetup()){leave();return false;}
  memoryStage("sdk-start-complete");
  wifi_scan_config_t config{};config.show_hidden=true;config.scan_type=WIFI_SCAN_TYPE_ACTIVE;
  config.scan_time.active.min=0;config.scan_time.active.max=120;
  s.result->struct_size=sizeof(*s.result);s.result->state=GARDEN_RADIO_SCAN_RUNNING;
  s.scanDeadline=esp_timer_get_time()+ScanTimeoutUs;
  if(!ensureLogsSuppressed() || !continueSetup()){leave();return false;}
  s.scanAttempted=true;s.scanList=true;
  if(!sdk("scan-start",sdkCall(ScanStart,[&](){return esp_wifi_scan_start(&config,false);})) || !continueSetup()){leave();return false;}
  RADIO_STAGE_LOG("radio wifi scan result=accepted");
  return true;
}
inline bool scanPoll(garden_radio_scan_result_v1* result){
  if(!result || result->struct_size<sizeof(*result) || s.closing)return false;
  if(s.operation!=Scan){*result={};result->struct_size=sizeof(*result);return true;}
  if(!s.result)return false;
  if(s.result->state==GARDEN_RADIO_SCAN_RUNNING){
    const uint32_t done=scanEvent.load(std::memory_order_acquire);
    if(done==2 || (!done && esp_timer_get_time()>=s.scanDeadline)){
      s.result->state=GARDEN_RADIO_SCAN_FAILED;RADIO_STAGE_LOG("radio wifi scan result=failed reason=%s",done==2?"scan-event":"timeout");
#if RISC_STAGE_LOGS
      if(done==2)RADIO_STAGE_LOG("radio wifi scan-event status=%lu",static_cast<unsigned long>(scanStatus.load(std::memory_order_relaxed)));
#endif
    }
    else if(done==1){
      wifi_ap_record_t records[GARDEN_RADIO_SCAN_MAX]{};uint16_t count=GARDEN_RADIO_SCAN_MAX;
      if(!sdk("scan-records",sdkCall(Records,[&](){return esp_wifi_scan_get_ap_records(&count,records);})) || count>GARDEN_RADIO_SCAN_MAX){
        s.result->state=GARDEN_RADIO_SCAN_FAILED;RADIO_STAGE_LOG("radio wifi scan result=failed step=records");
      }
      else{
        s.scanList=false;s.scanAttempted=false;s.result->count=uint8_t(count);s.result->state=GARDEN_RADIO_SCAN_DONE;
        for(uint16_t i=0;i<count;++i){
          auto& out=s.result->entries[i];std::memcpy(out.ssid,records[i].ssid,32);out.ssid[32]=0;
          out.rssi=records[i].rssi;out.channel=records[i].primary;out.auth=auth(records[i].authmode);
        }
        RADIO_STAGE_LOG("radio wifi scan result=complete count=%u",unsigned(count));
      }
      wipe(records,sizeof(records));
    }
    // Cancellation is explicit, including after a failure. Until it succeeds,
    // native idle remains false and resources remain owned by the caller.
  }
  *result=*s.result;return true;
}
inline bool scanCancel(){return leave();}
} }
#undef RADIO_STAGE_LOG
