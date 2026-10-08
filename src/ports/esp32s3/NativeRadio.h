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
inline bool sdkFailure(const char* step,esp_err_t code){
  (void)step;(void)code;
  RISC_STAGE_LOG("radio wifi failure step=%s code=%d",step,int(code));return false;
}
inline bool sdk(const char* step,esp_err_t code){return code==ESP_OK || sdkFailure(step,code);}
inline void linkStage(uint8_t state){
  (void)state;
#if RISC_STAGE_LOGS
  if(s.stageLink!=state){s.stageLink=state;RISC_STAGE_LOG("radio wifi link state=%s",state==2?"up":state==1?"joining":"down");}
#endif
}
static const char* const LogTags[]={"wifi","wifi_init","wifi_init_default","wifi_netif"};
inline bool suppressLogs(){
  // IDF's void setter can silently fail allocation. Verify every override
  // before any SDK initialization/configuration receives the station SSID.
  for(uint8_t i=0;i<4;++i){
    s.savedLogLevels[i]=esp_log_level_get(LogTags[i]);s.savedLogCount=i+1;
    esp_log_level_set(LogTags[i],ESP_LOG_NONE);
    if(esp_log_level_get(LogTags[i])!=ESP_LOG_NONE){RISC_STAGE_LOG("radio wifi failure step=suppress-logs tag=%u",unsigned(i));return false;}
  }
  return true;
}
inline bool ensureLogsSuppressed(){
  if(s.savedLogCount!=4)return false;
  for(const auto* tag:LogTags){
    if(esp_log_level_get(tag)!=ESP_LOG_NONE)esp_log_level_set(tag,ESP_LOG_NONE);
    if(esp_log_level_get(tag)!=ESP_LOG_NONE){RISC_STAGE_LOG("radio wifi failure step=keep-logs-suppressed");return false;}
  }
  return true;
}
inline bool restoreLogs(){
  // Called only after all radio/netif/hooks/event resources are gone. Retain a
  // failed restoration for retry, without restoring active-radio diagnostics.
  while(s.savedLogCount){
    const uint8_t i=s.savedLogCount-1;
    esp_log_level_set(LogTags[i],s.savedLogLevels[i]);
    if(esp_log_level_get(LogTags[i])!=s.savedLogLevels[i]){RISC_STAGE_LOG("radio wifi failure step=restore-logs tag=%u",unsigned(i));return false;}
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
  if(!token || token!=generation.load(std::memory_order_acquire) || base!=WIFI_EVENT)return;
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
  if(active)RISC_STAGE_LOG("radio wifi cleanup begin");
  // Revoke event authority before touching the SDK. A failed cleanup remains
  // closed and blocks all new starts until a later successful leave.
  generation.store(0,std::memory_order_release);eventOperation.store(None,std::memory_order_release);
  s.closing=true;
  if(s.wifi && !ensureLogsSuppressed())return false;
  if(s.scanAttempted){
    const esp_err_t result=esp_wifi_scan_stop();if(!wifiAbsent(result))return sdkFailure("scan-stop",result);
    s.scanAttempted=false;
  }
  if(s.scanList){
    const esp_err_t result=esp_wifi_clear_ap_list();if(!wifiAbsent(result))return sdkFailure("scan-list-clear",result);
    s.scanList=false;
  }
  if(s.connectAttempted){
    const esp_err_t result=esp_wifi_disconnect();
    if(!wifiAbsent(result) && result!=ESP_ERR_WIFI_NOT_CONNECT)return sdkFailure("disconnect",result);
    s.connectAttempted=false;
  }
  if(s.credentials){
    wifi_config_t empty{};
    // The only SDK credential copy is RAM-only; overwrite before stop/deinit.
    if(!sdk("config-clear",esp_wifi_set_config(WIFI_IF_STA,&empty)))return false;
    s.credentials=false;
  }
  if(s.startAttempted){
    const esp_err_t result=esp_wifi_stop();if(!wifiAbsent(result))return sdkFailure("stop",result);
    s.startAttempted=false;
  }
  if(s.wifi){
    const esp_err_t result=esp_wifi_deinit();
    if(result!=ESP_OK && result!=ESP_ERR_WIFI_NOT_INIT)return sdkFailure("deinit",result);
    s.wifi=false;
  }
  if(s.handler){
    if(!sdk("event-unregister",esp_event_handler_instance_unregister(WIFI_EVENT,ESP_EVENT_ANY_ID,s.handler)))return false;
    s.handler=nullptr;
  }
  if(s.attachAttempted){
    // IDF4.4 clears default registrations and destroys its driver even when
    // set_driver_config fails. Never call that destructive helper twice.
    const esp_err_t result=esp_wifi_clear_default_wifi_driver_and_handlers(s.netif);
    s.attachAttempted=false;
    if(result!=ESP_OK){s.driverClearPending=true;return sdkFailure("default-driver-clear",result);}
  }
  if(s.driverClearPending){
    esp_netif_driver_ifconfig_t empty{};
    if(!sdk("driver-config-clear",esp_netif_set_driver_config(s.netif,&empty)))return false;
    s.driverClearPending=false;
  }
  // Delete the owned event task/queue (including undelivered old SDK events)
  // before destroying the netif or allowing a new attempt. Generation tagging
  // alone cannot reject old queued events dispatched to a NEW registration.
  if(s.loop){
    if(!sdk("event-loop-delete",esp_event_loop_delete_default()))return false;
    s.loop=false;
  }
  if(s.netif){
    const esp_err_t result=esp_netif_dhcpc_stop(s.netif);
    if(result!=ESP_OK && result!=ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED && result!=ESP_ERR_ESP_NETIF_IF_NOT_READY)return sdkFailure("dhcp-stop",result);
    esp_netif_action_stop(s.netif,WIFI_EVENT,WIFI_EVENT_STA_STOP,nullptr);
    esp_netif_destroy(s.netif);s.netif=nullptr;
  }
  if(!restoreLogs())return false;
  if(s.result){wipe(s.result,sizeof(*s.result));heap_caps_free(s.result);s.result=nullptr;}
  s={};scanEvent.store(0,std::memory_order_release);joinFailed.store(0,std::memory_order_release);
  if(active)RISC_STAGE_LOG("radio wifi cleanup result=ok state=idle");
  return true;
}
inline bool begin(Operation operation){
  if(!idle() || nextGeneration==std::numeric_limits<uint32_t>::max()){RISC_STAGE_LOG("radio wifi begin result=rejected reason=not-idle-or-generation-exhausted");return false;}
  RISC_STAGE_LOG("radio wifi begin operation=%s",operation==Join?"connect":"scan");
  // esp_wifi_init returns success when somebody already initialized it; probe
  // before allocating anything so we never silently adopt another owner.
  wifi_mode_t existingMode{};
  const esp_err_t existing=esp_wifi_get_mode(&existingMode);
  if(existing!=ESP_ERR_WIFI_NOT_INIT)return sdkFailure("exclusive-owner-check",existing);
  s.operation=operation;
  if(!netifInitialized){if(!sdk("netif-init",esp_netif_init())){s={};return false;}netifInitialized=true;}
  // INVALID_STATE means somebody else owns the loop: never adopt/delete it.
  if(!sdk("event-loop-create",esp_event_loop_create_default())){s={};return false;}
  s.loop=true;
  if(!suppressLogs()){leave();return false;}
  esp_netif_config_t netifConfig=ESP_NETIF_DEFAULT_WIFI_STA();
  s.netif=esp_netif_new(&netifConfig);
  if(!s.netif){RISC_STAGE_LOG("radio wifi failure step=netif-new reason=allocation-failed");leave();return false;}
  s.attachAttempted=true;
  if(!sdk("netif-attach",esp_netif_attach_wifi_station(s.netif)) || !sdk("default-handlers",esp_wifi_set_default_wifi_sta_handlers())){leave();return false;}
  const uint32_t token=++nextGeneration;
  generation.store(token,std::memory_order_release);eventOperation.store(operation,std::memory_order_release);
  scanEvent.store(0,std::memory_order_release);joinFailed.store(0,std::memory_order_release);
#if RISC_STAGE_LOGS
  disconnectReason.store(0,std::memory_order_relaxed);
  scanStatus.store(0,std::memory_order_relaxed);
#endif
  if(!sdk("event-register",esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event,reinterpret_cast<void*>(uintptr_t(token)),&s.handler))){leave();return false;}
  wifi_init_config_t config=WIFI_INIT_CONFIG_DEFAULT();config.nvs_enable=false;
  // SDK attempts to unwind returned init failures internally; a hidden failed
  // unwind inside esp_wifi_init is not observable through the public API.
  if(!sdk("init",esp_wifi_init(&config))){leave();return false;}
  s.wifi=true;
  if(!sdk("storage-ram",esp_wifi_set_storage(WIFI_STORAGE_RAM)) || !sdk("station-mode",esp_wifi_set_mode(WIFI_MODE_STA))){leave();return false;}
  RISC_STAGE_LOG("radio wifi begin result=ready operation=%s",operation==Join?"connect":"scan");
  return true;
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
  if(!ensureLogsSuppressed()){wipe(&config,sizeof(config));leave();return false;}
  s.credentials=true;
  const esp_err_t configured=esp_wifi_set_config(WIFI_IF_STA,&config);
  wipe(&config,sizeof(config));
  if(!sdk("station-config",configured)){leave();return false;}
  if(!ensureLogsSuppressed()){leave();return false;}
  s.startAttempted=true;
  if(!sdk("start",esp_wifi_start())){leave();return false;}
  if(!ensureLogsSuppressed()){leave();return false;}
  s.connectAttempted=true;s.joinDeadline=esp_timer_get_time()+JoinTimeoutUs;
  if(!sdk("connect",esp_wifi_connect())){leave();return false;}
  RISC_STAGE_LOG("radio wifi connect result=accepted");
  return true;
}
inline bool state(uint8_t* status,int8_t* rssi){
  if(!status || !rssi)return false;
  *status=0;*rssi=0;
  if(s.closing)return false;
  if(s.operation!=Join || !s.wifi || !s.startAttempted)return true;
  if(joinFailed.load(std::memory_order_acquire)){
#if RISC_STAGE_LOGS
    if(!s.stageFailureReported){s.stageFailureReported=true;RISC_STAGE_LOG("radio wifi disconnect-event reason=%lu",static_cast<unsigned long>(disconnectReason.load(std::memory_order_relaxed)));}
#endif
    linkStage(0);return true;
  }
  wifi_ap_record_t ap{};esp_netif_ip_info_t ip{};
  if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK && esp_netif_is_netif_up(s.netif) &&
     esp_netif_get_ip_info(s.netif,&ip)==ESP_OK && ip.ip.addr && !joinFailed.load(std::memory_order_acquire)){
    s.established=true;*status=2;*rssi=ap.rssi;linkStage(2);return true;
  }
  if(joinFailed.load(std::memory_order_acquire) || s.established){linkStage(0);return true;}
  // Poll-driven deadline, with no background retry. Stop before returning DOWN
  // for a timed-out join, so a late SDK association cannot revive that attempt.
  if(esp_timer_get_time()>=s.joinDeadline){RISC_STAGE_LOG("radio wifi connect result=timeout");return leave();}
  *status=1;linkStage(1);return true;
}
inline bool addresses(uint8_t station[12],uint8_t ap[12]){
  if(!station || !ap)return false;
  std::memset(station,0,12);std::memset(ap,0,12);
  uint8_t status=0;int8_t rssi=0;if(!state(&status,&rssi))return false;
  if(status!=2)return true;
  esp_netif_ip_info_t ip{};if(!sdk("ip-info",esp_netif_get_ip_info(s.netif,&ip)))return false;
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
  if(!result){RISC_STAGE_LOG("radio wifi scan result=failed step=result-buffer reason=out-of-memory");return false;}
  if(!begin(Scan)){wipe(result,sizeof(*result));heap_caps_free(result);return false;}
  s.result=result;
  if(!ensureLogsSuppressed()){leave();return false;}
  s.startAttempted=true;
  if(!sdk("start",esp_wifi_start())){leave();return false;}
  wifi_scan_config_t config{};config.show_hidden=true;config.scan_type=WIFI_SCAN_TYPE_ACTIVE;
  config.scan_time.active.min=0;config.scan_time.active.max=120;
  s.result->struct_size=sizeof(*s.result);s.result->state=GARDEN_RADIO_SCAN_RUNNING;
  s.scanDeadline=esp_timer_get_time()+ScanTimeoutUs;
  if(!ensureLogsSuppressed()){leave();return false;}
  s.scanAttempted=true;s.scanList=true;
  if(!sdk("scan-start",esp_wifi_scan_start(&config,false))){leave();return false;}
  RISC_STAGE_LOG("radio wifi scan result=accepted");
  return true;
}
inline bool scanPoll(garden_radio_scan_result_v1* result){
  if(!result || result->struct_size<sizeof(*result) || s.closing)return false;
  if(s.operation!=Scan){*result={};result->struct_size=sizeof(*result);return true;}
  if(!s.result)return false;
  if(s.result->state==GARDEN_RADIO_SCAN_RUNNING){
    const uint32_t done=scanEvent.load(std::memory_order_acquire);
    if(done==2 || (!done && esp_timer_get_time()>=s.scanDeadline)){
      s.result->state=GARDEN_RADIO_SCAN_FAILED;RISC_STAGE_LOG("radio wifi scan result=failed reason=%s",done==2?"scan-event":"timeout");
#if RISC_STAGE_LOGS
      if(done==2)RISC_STAGE_LOG("radio wifi scan-event status=%lu",static_cast<unsigned long>(scanStatus.load(std::memory_order_relaxed)));
#endif
    }
    else if(done==1){
      wifi_ap_record_t records[GARDEN_RADIO_SCAN_MAX]{};uint16_t count=GARDEN_RADIO_SCAN_MAX;
      if(!sdk("scan-records",esp_wifi_scan_get_ap_records(&count,records)) || count>GARDEN_RADIO_SCAN_MAX){
        s.result->state=GARDEN_RADIO_SCAN_FAILED;RISC_STAGE_LOG("radio wifi scan result=failed step=records");
      }
      else{
        s.scanList=false;s.scanAttempted=false;s.result->count=uint8_t(count);s.result->state=GARDEN_RADIO_SCAN_DONE;
        for(uint16_t i=0;i<count;++i){
          auto& out=s.result->entries[i];std::memcpy(out.ssid,records[i].ssid,32);out.ssid[32]=0;
          out.rssi=records[i].rssi;out.channel=records[i].primary;out.auth=auth(records[i].authmode);
        }
        RISC_STAGE_LOG("radio wifi scan result=complete count=%u",unsigned(count));
      }
    }
    // Cancellation is explicit, including after a failure. Until it succeeds,
    // native idle remains false and resources remain owned by the caller.
  }
  *result=*s.result;return true;
}
inline bool scanCancel(){return leave();}
} }
