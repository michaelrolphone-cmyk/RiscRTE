#pragma once
/* Generic station-only IDF 4.4 adapter. Calls are serialized by the CPU port.
 * Exclusive radio/default-event-loop owner; never share with Arduino WiFi.
 * No app strings or code pointers are retained. See native_radio_shim/README.md
 * for the pinned SDK cleanup contracts and limits exercised by the host shim.
 * RF scans never block for completion, but synchronous SDK control/cleanup
 * calls have no cancellation API or enforceable 100-ms wall-clock bound. */
#include <RiscRadioScanV1.h>
#include <esp_event.h>
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
  garden_radio_scan_result_v1 result{};
};
static State s;
static const char* const LogTags[]={"wifi","wifi_init","wifi_init_default","wifi_netif"};
inline bool suppressLogs(){
  // IDF's void setter can silently fail allocation. Verify every override
  // before any SDK initialization/configuration receives the station SSID.
  for(uint8_t i=0;i<4;++i){
    s.savedLogLevels[i]=esp_log_level_get(LogTags[i]);s.savedLogCount=i+1;
    esp_log_level_set(LogTags[i],ESP_LOG_NONE);
    if(esp_log_level_get(LogTags[i])!=ESP_LOG_NONE)return false;
  }
  return true;
}
inline bool ensureLogsSuppressed(){
  if(s.savedLogCount!=4)return false;
  for(const auto* tag:LogTags){
    if(esp_log_level_get(tag)!=ESP_LOG_NONE)esp_log_level_set(tag,ESP_LOG_NONE);
    if(esp_log_level_get(tag)!=ESP_LOG_NONE)return false;
  }
  return true;
}
inline bool restoreLogs(){
  // Called only after all radio/netif/hooks/event resources are gone. Retain a
  // failed restoration for retry, without restoring active-radio diagnostics.
  while(s.savedLogCount){
    const uint8_t i=s.savedLogCount-1;
    esp_log_level_set(LogTags[i],s.savedLogLevels[i]);
    if(esp_log_level_get(LogTags[i])!=s.savedLogLevels[i])return false;
    --s.savedLogCount;
  }
  return true;
}
// TCP/IP infrastructure cannot be deinitialized in IDF 4.4. It owns no radio,
// station interface or app callback after leave; keep its one-time init flag.
static bool netifInitialized=false;
static uint32_t nextGeneration=0;
static std::atomic<uint32_t> generation{0},eventOperation{None},scanEvent{0},joinFailed{0};
inline void wipe(void* data,size_t count){auto* p=static_cast<volatile uint8_t*>(data);while(count--)*p++=0;}
inline size_t boundedLength(const char* value,size_t maximum){
  if(!value)return maximum+1;
  size_t n=0;while(n<=maximum && value[n])++n;return n;
}
inline void event(void* argument,esp_event_base_t base,int32_t id,void* data){
  const uint32_t token=uint32_t(reinterpret_cast<uintptr_t>(argument));
  if(!token || token!=generation.load(std::memory_order_acquire) || base!=WIFI_EVENT)return;
  const auto operation=eventOperation.load(std::memory_order_acquire);
  if(operation==Join && id==WIFI_EVENT_STA_DISCONNECTED)joinFailed.store(1,std::memory_order_release);
  if(operation==Scan && id==WIFI_EVENT_SCAN_DONE && data){
    const auto* done=static_cast<const wifi_event_sta_scan_done_t*>(data);
    scanEvent.store(done->status?2:1,std::memory_order_release);
  }
  // Never reconnect, copy credentials, call SDK functions or touch s here.
}
inline bool idle(){return !s.loop && !s.wifi && !s.netif && !s.handler && !s.closing && !s.savedLogCount;}
inline bool wifiAbsent(esp_err_t result){return result==ESP_OK || result==ESP_ERR_WIFI_NOT_INIT || result==ESP_ERR_WIFI_NOT_STARTED;}
inline bool leave(){
  // Revoke event authority before touching the SDK. A failed cleanup remains
  // closed and blocks all new starts until a later successful leave.
  generation.store(0,std::memory_order_release);eventOperation.store(None,std::memory_order_release);
  s.closing=true;
  if(s.wifi && !ensureLogsSuppressed())return false;
  if(s.scanAttempted){
    if(!wifiAbsent(esp_wifi_scan_stop()))return false;
    s.scanAttempted=false;
  }
  if(s.scanList){
    if(!wifiAbsent(esp_wifi_clear_ap_list()))return false;
    s.scanList=false;
  }
  if(s.connectAttempted){
    const esp_err_t result=esp_wifi_disconnect();
    if(!wifiAbsent(result) && result!=ESP_ERR_WIFI_NOT_CONNECT)return false;
    s.connectAttempted=false;
  }
  if(s.credentials){
    wifi_config_t empty{};
    // The only SDK credential copy is RAM-only; overwrite before stop/deinit.
    if(esp_wifi_set_config(WIFI_IF_STA,&empty)!=ESP_OK)return false;
    s.credentials=false;
  }
  if(s.startAttempted){
    if(!wifiAbsent(esp_wifi_stop()))return false;
    s.startAttempted=false;
  }
  if(s.wifi){
    const esp_err_t result=esp_wifi_deinit();
    if(result!=ESP_OK && result!=ESP_ERR_WIFI_NOT_INIT)return false;
    s.wifi=false;
  }
  if(s.handler){
    if(esp_event_handler_instance_unregister(WIFI_EVENT,ESP_EVENT_ANY_ID,s.handler)!=ESP_OK)return false;
    s.handler=nullptr;
  }
  if(s.attachAttempted){
    // IDF4.4 clears default registrations and destroys its driver even when
    // set_driver_config fails. Never call that destructive helper twice.
    const esp_err_t result=esp_wifi_clear_default_wifi_driver_and_handlers(s.netif);
    s.attachAttempted=false;
    if(result!=ESP_OK){s.driverClearPending=true;return false;}
  }
  if(s.driverClearPending){
    esp_netif_driver_ifconfig_t empty{};
    if(esp_netif_set_driver_config(s.netif,&empty)!=ESP_OK)return false;
    s.driverClearPending=false;
  }
  // Delete the owned event task/queue (including undelivered old SDK events)
  // before destroying the netif or allowing a new attempt. Generation tagging
  // alone cannot reject old queued events dispatched to a NEW registration.
  if(s.loop){
    if(esp_event_loop_delete_default()!=ESP_OK)return false;
    s.loop=false;
  }
  if(s.netif){
    const esp_err_t result=esp_netif_dhcpc_stop(s.netif);
    if(result!=ESP_OK && result!=ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED && result!=ESP_ERR_ESP_NETIF_IF_NOT_READY)return false;
    esp_netif_action_stop(s.netif,WIFI_EVENT,WIFI_EVENT_STA_STOP,nullptr);
    esp_netif_destroy(s.netif);s.netif=nullptr;
  }
  if(!restoreLogs())return false;
  s={};scanEvent.store(0,std::memory_order_release);joinFailed.store(0,std::memory_order_release);
  return true;
}
inline bool begin(Operation operation){
  if(!idle() || nextGeneration==std::numeric_limits<uint32_t>::max())return false;
  // esp_wifi_init returns success when somebody already initialized it; probe
  // before allocating anything so we never silently adopt another owner.
  wifi_mode_t existingMode{};
  if(esp_wifi_get_mode(&existingMode)!=ESP_ERR_WIFI_NOT_INIT)return false;
  s.operation=operation;
  if(!netifInitialized){if(esp_netif_init()!=ESP_OK){s={};return false;}netifInitialized=true;}
  // INVALID_STATE means somebody else owns the loop: never adopt/delete it.
  if(esp_event_loop_create_default()!=ESP_OK){s={};return false;}
  s.loop=true;
  if(!suppressLogs()){leave();return false;}
  esp_netif_config_t netifConfig=ESP_NETIF_DEFAULT_WIFI_STA();
  s.netif=esp_netif_new(&netifConfig);
  if(!s.netif){leave();return false;}
  s.attachAttempted=true;
  if(esp_netif_attach_wifi_station(s.netif)!=ESP_OK || esp_wifi_set_default_wifi_sta_handlers()!=ESP_OK){leave();return false;}
  const uint32_t token=++nextGeneration;
  generation.store(token,std::memory_order_release);eventOperation.store(operation,std::memory_order_release);
  scanEvent.store(0,std::memory_order_release);joinFailed.store(0,std::memory_order_release);
  if(esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event,reinterpret_cast<void*>(uintptr_t(token)),&s.handler)!=ESP_OK){leave();return false;}
  wifi_init_config_t config=WIFI_INIT_CONFIG_DEFAULT();config.nvs_enable=false;
  // SDK attempts to unwind returned init failures internally; a hidden failed
  // unwind inside esp_wifi_init is not observable through the public API.
  if(esp_wifi_init(&config)!=ESP_OK){leave();return false;}
  s.wifi=true;
  if(esp_wifi_set_storage(WIFI_STORAGE_RAM)!=ESP_OK || esp_wifi_set_mode(WIFI_MODE_STA)!=ESP_OK){leave();return false;}
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
  if(configured!=ESP_OK){leave();return false;}
  if(!ensureLogsSuppressed()){leave();return false;}
  s.startAttempted=true;
  if(esp_wifi_start()!=ESP_OK){leave();return false;}
  if(!ensureLogsSuppressed()){leave();return false;}
  s.connectAttempted=true;s.joinDeadline=esp_timer_get_time()+JoinTimeoutUs;
  if(esp_wifi_connect()!=ESP_OK){leave();return false;}
  return true;
}
inline bool state(uint8_t* status,int8_t* rssi){
  if(!status || !rssi)return false;
  *status=0;*rssi=0;
  if(s.closing)return false;
  if(s.operation!=Join || !s.wifi || !s.startAttempted || joinFailed.load(std::memory_order_acquire))return true;
  wifi_ap_record_t ap{};esp_netif_ip_info_t ip{};
  if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK && esp_netif_is_netif_up(s.netif) &&
     esp_netif_get_ip_info(s.netif,&ip)==ESP_OK && ip.ip.addr && !joinFailed.load(std::memory_order_acquire)){
    s.established=true;*status=2;*rssi=ap.rssi;return true;
  }
  if(joinFailed.load(std::memory_order_acquire) || s.established)return true;
  // Poll-driven deadline, with no background retry. Stop before returning DOWN
  // for a timed-out join, so a late SDK association cannot revive that attempt.
  if(esp_timer_get_time()>=s.joinDeadline)return leave();
  *status=1;return true;
}
inline bool addresses(uint8_t station[12],uint8_t ap[12]){
  if(!station || !ap)return false;
  std::memset(station,0,12);std::memset(ap,0,12);
  uint8_t status=0;int8_t rssi=0;if(!state(&status,&rssi))return false;
  if(status!=2)return true;
  esp_netif_ip_info_t ip{};if(esp_netif_get_ip_info(s.netif,&ip)!=ESP_OK)return false;
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
  if(!begin(Scan))return false;
  if(!ensureLogsSuppressed()){leave();return false;}
  s.startAttempted=true;
  if(esp_wifi_start()!=ESP_OK){leave();return false;}
  wifi_scan_config_t config{};config.show_hidden=true;config.scan_type=WIFI_SCAN_TYPE_ACTIVE;
  config.scan_time.active.min=0;config.scan_time.active.max=120;
  s.result={};s.result.struct_size=sizeof(s.result);s.result.state=GARDEN_RADIO_SCAN_RUNNING;
  s.scanDeadline=esp_timer_get_time()+ScanTimeoutUs;
  if(!ensureLogsSuppressed()){leave();return false;}
  s.scanAttempted=true;s.scanList=true;
  if(esp_wifi_scan_start(&config,false)!=ESP_OK){leave();return false;}
  return true;
}
inline bool scanPoll(garden_radio_scan_result_v1* result){
  if(!result || result->struct_size<sizeof(*result) || s.closing)return false;
  if(s.operation!=Scan){*result={};result->struct_size=sizeof(*result);return true;}
  if(s.result.state==GARDEN_RADIO_SCAN_RUNNING){
    const uint32_t done=scanEvent.load(std::memory_order_acquire);
    if(done==2 || (!done && esp_timer_get_time()>=s.scanDeadline))s.result.state=GARDEN_RADIO_SCAN_FAILED;
    else if(done==1){
      wifi_ap_record_t records[GARDEN_RADIO_SCAN_MAX]{};uint16_t count=GARDEN_RADIO_SCAN_MAX;
      if(esp_wifi_scan_get_ap_records(&count,records)!=ESP_OK || count>GARDEN_RADIO_SCAN_MAX)s.result.state=GARDEN_RADIO_SCAN_FAILED;
      else{
        s.scanList=false;s.scanAttempted=false;s.result.count=uint8_t(count);s.result.state=GARDEN_RADIO_SCAN_DONE;
        for(uint16_t i=0;i<count;++i){
          auto& out=s.result.entries[i];std::memcpy(out.ssid,records[i].ssid,32);out.ssid[32]=0;
          out.rssi=records[i].rssi;out.channel=records[i].primary;out.auth=auth(records[i].authmode);
        }
      }
    }
    // Cancellation is explicit, including after a failure. Until it succeeds,
    // native idle remains false and resources remain owned by the caller.
  }
  *result=s.result;return true;
}
inline bool scanCancel(){return leave();}
} }
