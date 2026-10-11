#include "ports/esp32s3/NativeRadio.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "radio_stage_capture.h"
using namespace RiscCpu::NativeRadio;
const char* WIFI_EVENT="wifi";
static std::vector<std::string> calls;
static std::string failure,failure2;
static esp_err_t failureResult=ESP_FAIL;
static bool loop=false,wifi=false,started=false,associated=false,scanning=false,list=false,defaults=false,driver=false;
static bool badCount=false,startEmitsDone=false,externalLoop=false,attachCalled=false;
static bool initChangesLogs=false,configChangesLogs=false,startChangesLogs=false,blockLogRemute=false;
static int netifInitCount=0,clears=0,connects=0;
static std::atomic<int64_t> now{0};
static esp_netif_t* netif=nullptr;
static esp_netif_ip_info_t ip{};
static wifi_config_t copied{};
static std::vector<wifi_ap_record_t> found;
struct Handler {esp_event_handler_t function;void* argument;};
static Handler handler{};
static std::vector<Handler> oldHandlers;
static std::vector<wifi_event_sta_scan_done_t> queued;
// Read only while the caller's local config is still alive, from start().
static const wifi_config_t* pendingCredentialWipe=nullptr;
static bool credentialWipeObserved=false;
static bool allocationFails=false,workspaceFails=false;
static void* workspaceAllocation=nullptr;static size_t workspaceBytes=0;
static unsigned workspaceAllocations=0,workspaceFrees=0;
static void* scanAllocation=nullptr;
static unsigned scanAllocations=0,scanFrees=0;
// Keep the boot-resident control state small on both host and Xtensa. The
// public 600-byte scan record belongs only to an active scan's native heap.
static_assert(sizeof(State)<=112,"radio control state must not embed scan cache");
static_assert(sizeof(garden_radio_scan_result_v1)==600,"bounded scan allocation");
static const esp_log_level_t originalLogLevels[4]={ESP_LOG_INFO,ESP_LOG_DEBUG,ESP_LOG_WARN,ESP_LOG_ERROR};
static esp_log_level_t logLevels[4]={ESP_LOG_INFO,ESP_LOG_DEBUG,ESP_LOG_WARN,ESP_LOG_ERROR};
static const esp_log_level_t unrelatedLogLevel=ESP_LOG_VERBOSE;
static void logsQuiet(){for(auto level:logLevels)assert(level==ESP_LOG_NONE);}
static void logsRestored(){for(unsigned i=0;i<4;++i)assert(logLevels[i]==originalLogLevels[i]);}

static void (*sdkBoundary)(const char*)=nullptr;
static esp_err_t call(const char* name){if(sdkBoundary)sdkBoundary(name);calls.emplace_back(name);return failure==name || failure2==name?failureResult:ESP_OK;}
static bool called(const char* name){return std::find(calls.begin(),calls.end(),name)!=calls.end();}
static bool zero(const void* p,size_t n){const auto* bytes=static_cast<const uint8_t*>(p);for(size_t i=0;i<n;++i)if(bytes[i])return false;return true;}
static unsigned heapQueries;
size_t heap_caps_get_free_size(uint32_t caps){++heapQueries;return caps&(MALLOC_CAP_SPIRAM)?2000000u:100000u;}
size_t heap_caps_get_largest_free_block(uint32_t caps){++heapQueries;return caps&MALLOC_CAP_DMA?32000u:caps&MALLOC_CAP_SPIRAM?1000000u:50000u;}
void* heap_caps_calloc(size_t count,size_t size,uint32_t capabilities){
 if(capabilities==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)){
  assert(count==1 && size>=1384 && size<=2304 && !workspaceAllocation);++workspaceAllocations;
  if(workspaceFails)return nullptr;
  workspaceBytes=size;workspaceAllocation=std::calloc(count,size);assert(workspaceAllocation);return workspaceAllocation;
 }
 assert(count==1 && size==sizeof(garden_radio_scan_result_v1) && capabilities==MALLOC_CAP_8BIT);
 assert(!scanAllocation);++scanAllocations;
 if(allocationFails)return nullptr;
 scanAllocation=std::calloc(count,size);assert(scanAllocation);return scanAllocation;
}
void heap_caps_free(void* pointer){
 if(pointer && pointer==workspaceAllocation){assert(zero(pointer,workspaceBytes));std::free(pointer);workspaceAllocation=nullptr;++workspaceFrees;return;}
 assert(pointer && pointer==scanAllocation && zero(pointer,sizeof(garden_radio_scan_result_v1)));
 std::free(pointer);scanAllocation=nullptr;++scanFrees;
}
static void emit(int32_t id,void* data=nullptr){if(handler.function)handler.function(handler.argument,WIFI_EVENT,id,data);}
static void emitScan(uint32_t status=0){wifi_event_sta_scan_done_t done{status,uint8_t(found.size()),1};emit(WIFI_EVENT_SCAN_DONE,&done);scanning=false;}
static unsigned logIndex(const char* tag){for(unsigned i=0;i<4;++i)if(!strcmp(tag,LogTags[i]))return i;assert(false);return 0;}
esp_log_level_t esp_log_level_get(const char* tag){if(!strcmp(tag,"unrelated"))return unrelatedLogLevel;return logLevels[logIndex(tag)];}
void esp_log_level_set(const char* tag,esp_log_level_t level){
 const unsigned i=logIndex(tag);const bool suppress=level==ESP_LOG_NONE;
 if(!suppress)assert(!wifi && !netif && !loop && !driver && !handler.function);
 if(call((std::string(suppress?"log_suppress_":"log_restore_")+tag).c_str())==ESP_OK)logLevels[i]=level;
}
int64_t esp_timer_get_time(){return now;}
esp_err_t esp_netif_init(){auto result=call("netif_init");if(result==ESP_OK)++netifInitCount;return result;}
esp_err_t esp_event_loop_create_default(){assert(!loop);if(externalLoop){call("loop_create");return ESP_ERR_INVALID_STATE;}auto result=call("loop_create");if(result==ESP_OK)loop=true;return result;}
esp_err_t esp_event_loop_delete_default(){assert(loop && !wifi);auto result=call("loop_delete");if(result==ESP_OK){loop=false;handler={};queued.clear();defaults=false;}return result;}
esp_netif_t* esp_netif_new(const esp_netif_config_t* config){assert(loop && !netif && config->station==1);if(call("netif_new")!=ESP_OK)return nullptr;netif=new esp_netif_t;return netif;}
esp_err_t esp_netif_attach_wifi_station(esp_netif_t* n){assert(n==netif && !driver);attachCalled=true;if(failure=="attach_no_driver")return call("attach_no_driver");driver=true;n->attached=true;return call("attach");}
esp_err_t esp_wifi_set_default_wifi_sta_handlers(){assert(driver);defaults=true;return call("defaults");}
esp_err_t esp_wifi_clear_default_wifi_driver_and_handlers(void* n){assert(n==netif && attachCalled);attachCalled=false;++clears;driver=false;defaults=false;const auto result=call("driver_clear");if(result==ESP_OK)netif->attached=false;return result;}
esp_err_t esp_netif_set_driver_config(esp_netif_t* n,const esp_netif_driver_ifconfig_t* c){assert(n==netif && zero(c,sizeof(*c)) && !driver);auto result=call("driver_config_clear");if(result==ESP_OK)n->attached=false;return result;}
esp_err_t esp_event_handler_instance_register(esp_event_base_t base,int32_t id,esp_event_handler_t f,void* arg,esp_event_handler_instance_t* out){assert(base==WIFI_EVENT && id==ESP_EVENT_ANY_ID && loop && !handler.function);auto result=call("register");if(result==ESP_OK){handler={f,arg};oldHandlers.push_back(handler);*out=reinterpret_cast<void*>(uintptr_t(42));}return result;}
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base,int32_t id,esp_event_handler_instance_t h){assert(base==WIFI_EVENT && id==ESP_EVENT_ANY_ID && h && handler.function);auto result=call("unregister");if(result==ESP_OK)handler={};return result;}
esp_err_t esp_netif_dhcpc_stop(esp_netif_t* n){assert(n==netif && !loop && !wifi);auto result=call("dhcp_stop");if(result==ESP_OK)n->dhcp=false;return result;}
void esp_netif_action_stop(void* n,esp_event_base_t base,int32_t id,void* data){assert(n==netif && base==WIFI_EVENT && id==WIFI_EVENT_STA_STOP && !data && !loop);call("netif_stop");netif->up=false;}
void esp_netif_destroy(esp_netif_t* n){assert(n==netif && !n->attached && !n->dhcp && !n->up && !loop && !driver);call("netif_destroy");delete n;netif=nullptr;}
esp_err_t esp_wifi_get_mode(wifi_mode_t*){auto result=call("get_mode");return result!=ESP_OK?result:wifi?ESP_OK:ESP_ERR_WIFI_NOT_INIT;}
esp_err_t esp_wifi_init(const wifi_init_config_t* c){logsQuiet();assert(!wifi && loop && netif && c->nvs_enable==0);auto result=call("wifi_init");if(result==ESP_OK)wifi=true;if(initChangesLogs){logLevels[0]=ESP_LOG_INFO;if(blockLogRemute)failure="log_suppress_wifi";}return result;}
esp_err_t esp_wifi_deinit(){logsQuiet();assert(wifi && !started);auto result=call("deinit");if(result==ESP_OK){wifi=false;assert(zero(&copied,sizeof(copied)));}return result;}
esp_err_t esp_wifi_set_storage(wifi_storage_t value){assert(wifi && value==WIFI_STORAGE_RAM);return call("storage_ram");}
esp_err_t esp_wifi_set_mode(wifi_mode_t value){assert(wifi && value==WIFI_MODE_STA);return call("station_mode");}
esp_err_t esp_wifi_set_config(wifi_interface_t iface,const wifi_config_t* config){
 logsQuiet();assert(wifi && iface==WIFI_IF_STA);
 const bool empty=zero(config,sizeof(*config));auto result=call(empty?"credentials_clear":"credentials_copy");
 if(result==ESP_OK)copied=*config;
 if(empty)pendingCredentialWipe=nullptr;
 if(!empty){pendingCredentialWipe=config;if(configChangesLogs)logLevels[0]=ESP_LOG_DEBUG;}
 return result;
}
esp_err_t esp_wifi_start(){
 logsQuiet();assert(wifi && !started);if(pendingCredentialWipe){assert(zero(pendingCredentialWipe,sizeof(*pendingCredentialWipe)));pendingCredentialWipe=nullptr;credentialWipeObserved=true;}
 auto result=call("start");if(result==ESP_OK)started=true;if(startChangesLogs)logLevels[0]=ESP_LOG_WARN;return result;
}
esp_err_t esp_wifi_stop(){assert(wifi);auto result=call("stop");if(result==ESP_OK){started=false;associated=false;scanning=false;}return result;}
esp_err_t esp_wifi_connect(){logsQuiet();assert(wifi && started && copied.sta.ssid[0]);++connects;return call("connect");}
esp_err_t esp_wifi_disconnect(){logsQuiet();assert(wifi);auto result=call("disconnect");if(result==ESP_OK){associated=false;emit(WIFI_EVENT_STA_DISCONNECTED);}return result;}
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t* out){assert(wifi && started);if(call("ap_info")!=ESP_OK || !associated)return ESP_ERR_WIFI_NOT_CONNECT;out->rssi=-42;return ESP_OK;}
bool esp_netif_is_netif_up(esp_netif_t* n){assert(n==netif);return n->up;}
esp_err_t esp_netif_get_ip_info(esp_netif_t* n,esp_netif_ip_info_t* out){assert(n==netif);auto result=call("ip_info");if(result==ESP_OK)*out=ip;return result;}
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t* c,bool block){logsQuiet();assert(wifi && started && !associated && !block && !c->ssid && !c->bssid && !c->channel && c->show_hidden && c->scan_type==WIFI_SCAN_TYPE_ACTIVE && !c->scan_time.active.min && c->scan_time.active.max==120);auto result=call("scan_start");if(result==ESP_OK){scanning=true;list=true;if(startEmitsDone)emitScan();}return result;}
esp_err_t esp_wifi_scan_stop(){assert(wifi && started);auto result=call("scan_stop");if(result==ESP_OK){scanning=false;queued.push_back({1,0,1});}return result;}
esp_err_t esp_wifi_clear_ap_list(){assert(wifi && started);auto result=call("list_clear");if(result==ESP_OK)list=false;return result;}
esp_err_t esp_wifi_scan_get_ap_records(uint16_t* count,wifi_ap_record_t* out){assert(wifi && started && *count==16 && !scanning);auto result=call("records");if(result!=ESP_OK)return result;*count=std::min<size_t>(*count,found.size());std::copy_n(found.begin(),*count,out);if(badCount)*count=17;list=false;return ESP_OK;}
static void reset(){
 failure.clear();failure2.clear();assert(leave());assert(idle() && !wifi && !netif && !loop && !driver && !handler.function && !list && !scanAllocation);
 logsRestored();assert(esp_log_level_get("unrelated")==ESP_LOG_VERBOSE);
 calls.clear();now=0;ip={};found.clear();badCount=startEmitsDone=externalLoop=false;pendingCredentialWipe=nullptr;credentialWipeObserved=false;copied={};allocationFails=false;initChangesLogs=configChangesLogs=startChangesLogs=blockLogRemute=false;
}
static garden_radio_scan_result_v1 poll(){garden_radio_scan_result_v1 value{};value.struct_size=sizeof(value);assert(scanPoll(&value));return value;}
static uint8_t status(){uint8_t value=99;int8_t rssi=99;assert(state(&value,&rssi));assert((value==2 && rssi==-42) || (value!=2 && !rssi));return value;}
