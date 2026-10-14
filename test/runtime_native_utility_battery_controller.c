/* Actual Battery toggle controller, real Runtime storage. Broadcast hardware
 * pause/result hooks are synthetic; this bridge does not claim adapter/RF QA. */
#include "runtime_native_utility_bridge.h"
#define PORTABLE_BLE_BROADCAST
#define PORTABLE_NATIVE_TIME_TOOLBAR
#include UTILITY_BATTERY_SOURCE
static bool bridge_retained;static unsigned bridge_pauses,bridge_results;
bool portable_adapter_retained(void){return bridge_retained;}
void portable_adapter_retain(void){bridge_retained=true;assert(!"unexpected Battery retention");}
bool portable_broadcast_stop(void){++bridge_pauses;return true;}
bool portable_broadcast_preference_result(bool enabled,bool saved){(void)enabled;++bridge_results;assert(saved);return saved;}
static bool bridge_fixture_acquire(const char*n,uint32_t v,uint64_t id,risc_runtime_capability_v1*g){(void)n;(void)v;(void)id;(void)g;assert(false);return false;}
static bool bridge_fixture_release(risc_runtime_capability_v1*g){(void)g;assert(false);return false;}
const char *native_extra_key(unsigned i){return i?NULL:"ble_broadcast";}
unsigned native_extra_intents(void){return 3;}
void runtime_native_settings_preferences(const risc_key_value_v1 *view,unsigned ignored){(void)ignored;bool enabled=false;assert(portable_broadcast_control_load(view,&enabled)&&enabled);}
void runtime_native_settings_controller(const risc_runtime_api_v1 *rt,unsigned phase,const char *scenario){
 (void)scenario;bridge_begin(rt);power_preference=(utility_preference_atomic){0};
 if(!phase){bridge_plan_begin();for(unsigned i=0;i<3;++i){bool on=i!=1;uint8_t b[]={0x62,1,on?1:0,(uint8_t)((on?1:0)^0xa5)};bridge_expect_put("ble_broadcast",b,4,0);assert(power_broadcast_save(on));}bridge_plan_end(3);assert(bridge_pauses==3&&bridge_results==3);}
 native_extra_verify_reads(bridge_broker);assert(!bridge_retained&&!power_preference.edit.token&&!power_preference.grant.api);bridge_clean();
 puts(phase?"Actual Battery: fresh broadcast codec reads restored real-broker value PASS":"Actual Battery: Enable/Disable/Enable actual controller, three exact real-broker PFI intents PASS");
}
