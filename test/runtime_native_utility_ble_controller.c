/* Actual BLE name/clear and radio enable/compensation controllers. Only text
 * entry and physical HCI effects are synthetic. No fixture storage is linked. */
#include "runtime_native_utility_bridge.h"
#define PORTABLE_APP_OWNS_TOUCH_CHROME
#define PORTABLE_RETURN_APP "springboard.elf"
#define PORTABLE_APP_SLEEP_LOCAL
#define PORTABLE_ALARM_CLIENT
#include UTILITY_BLE_SOURCE
static bool text_suspended,text_live,text_retained,fail_enable;static unsigned text_sessions,text_releases,hci_changes,hci_grants;static uint8_t hci_enabled;
static risc_text_entry_state_v1 text_result;
int portable_text_adapter_suspend(void){assert(!text_suspended&&!token&&!acquired);text_suspended=true;return RISC_TEXT_ENTRY_OK;}
int portable_text_adapter_resume(void){assert(text_suspended&&!text_live);text_suspended=false;return RISC_TEXT_ENTRY_OK;}
int portable_text_adapter_attention(void){assert(text_suspended);return 0;}
void portable_text_adapter_retain(void){text_retained=true;assert(!"unexpected BLE retention");}
bool portable_text_adapter_retained(void){return text_retained;}
bool portable_app_sleep_retained(void){return false;}
static int32_t text_open(void*c,const risc_text_entry_request_v1*q,uint64_t*t){(void)c;assert(text_suspended&&!text_live&&q->capacity==sizeof(editing));text_live=true;*t=++text_sessions;text_result=(risc_text_entry_state_v1){.struct_size=sizeof(text_result),.state=RISC_TEXT_ENTRY_EDITING,.revision=1};strcpy(text_result.text,q->text);return 0;}
static int32_t text_poll(void*c,uint64_t t,risc_text_entry_state_v1*s){(void)c;assert(t==text_sessions&&text_live);*s=text_result;return 0;}
static int32_t text_close(void*c,uint64_t t){(void)c;assert(t==text_sessions&&text_live);text_live=false;return 0;}
static const risc_text_entry_api_v1 text_api={1,sizeof(text_api),NULL,text_open,text_poll,text_close};
static bool hci_set(void*c,bool on){(void)c;++hci_changes;if(on&&fail_enable)return false;hci_enabled=on;return true;}
static bool hci_status(void*c,uint8_t*out){(void)c;*out=hci_enabled;return true;}
static const portable_bluetooth_control_v1 hci_api={.api_version=1,.struct_size=sizeof(hci_api),.set_enabled=hci_set,.status=hci_status};
static bool bridge_fixture_acquire(const char*n,uint32_t v,uint64_t id,risc_runtime_capability_v1*g){assert(v==1);if(!strcmp(n,RISC_TEXT_ENTRY_CAPABILITY)){assert(!id);g->api=&text_api;}else{assert(!strcmp(n,"bluetooth.hci")&&id==16);g->api=&hci_api;++hci_grants;}g->slot=100;g->generation=1;return true;}
static bool bridge_fixture_release(risc_runtime_capability_v1*g){if(g->api==&text_api){assert(!text_live&&text_suspended);++text_releases;}else{assert(g->api==&hci_api&&hci_grants);--hci_grants;}*g=(risc_runtime_capability_v1){.struct_size=sizeof(*g)};return true;}
static uint32_t now(void){return 100;}
static const t5_app_api_v1 app_api={.abi_version=1,.struct_size=sizeof(app_api),.millis=now};
static const uint8_t address_a[6]={0x12,0x34,0x56,0x78,0x9a,0xbc},address_b[6]={0xfe,0xdc,0xba,0x98,0x76,0x54};
static const char*const selected_keys[]={"bs2123456789abc","bs1fedcba987654","quick_radio"};
const char *native_extra_key(unsigned i){return i<3?selected_keys[i]:NULL;}
unsigned native_extra_intents(void){return 6;}
static void alias_edit(unsigned index,const char *draft,const char *saved){
 uint8_t b[40];ble_device*d=&scan.devices[index];assert(ble_alias_encode(d->address_type,d->address,saved,b));char key[16];ble_alias_key(d->address_type,d->address,key);bridge_expect_put(key,b,40,0);
 detail_index=index;detail=true;begin_name();assert(naming&&text_live);strcpy(text_result.text,draft);text_result.state=RISC_TEXT_ENTRY_ACCEPTED;name_step();assert(!naming&&!text_live&&!text_suspended&&!strcmp(aliases[index],saved)&&!name_save_failed&&!uncertain);
}
void runtime_native_settings_preferences(const risc_key_value_v1 *view,unsigned ignored){(void)ignored;char name[25];uint8_t flags=0;assert(ble_alias_load(view,2,address_a,name)==1&&!strcmp(name,"Kitchen"));assert(ble_alias_load(view,1,address_b,name)==1&&!name[0]);assert(portable_radio_load(view,&flags)&&flags==3);}
void runtime_native_settings_controller(const risc_runtime_api_v1 *rt,unsigned phase,const char *scenario){
 (void)scenario;bridge_begin(rt);runtime=&bridge_runtime;app=&app_api;scan=(ble_scan){0};name_preference=(utility_preference_atomic){0};radio_preference=(utility_preference_atomic){0};radio_compensation=(portable_preference_rollback){0};name_client=(portable_text_client){0};
 scan.count=2;scan.devices[0].address_type=2;memcpy(scan.devices[0].address,address_a,6);scan.devices[1].address_type=1;memcpy(scan.devices[1].address,address_b,6);
 if(!phase){
  bridge_plan_begin();alias_edit(0,"  Kitchen  ","Kitchen");alias_edit(1,"Temporary","Temporary");alias_edit(1,"","");
  const uint8_t enabled_record[]={0x51,1,3,0xa6};unsigned origin=bridge_expect_put("quick_radio",enabled_record,4,0);assert(!bridge_intents[origin][10]);
  fail_enable=true;paper_enable_needed=true;paper_enable();assert(!uncertain&&!hci_enabled&&hci_changes==2&&!hci_grants&&radio_compensation.kind==RISC_ATOMIC_DELETE);
  bridge_expect_rollback(origin,&radio_preference.edit.receipt);assert(!radio_compensation.edit.token);
  bridge_expect_put("quick_radio",enabled_record,4,0);fail_enable=false;paper_enable();assert(!uncertain&&hci_enabled&&hci_changes==3&&!paper_enable_needed&&!hci_grants);bridge_plan_end(6);
  assert(text_sessions==3&&text_releases==3);
 }
 /* The production scanner's fresh alias read path acquires legacy read-through. */
 memset(aliases,0,sizeof(aliases));memset(alias_state,0,sizeof(alias_state));load_alias(0);load_alias(1);assert(alias_state[0]==1&&alias_state[1]==1&&!strcmp(aliases[0],"Kitchen")&&!aliases[1][0]);assert(policy_allowed());
 native_extra_verify_reads(bridge_broker);assert(!uncertain&&!text_retained&&!name_preference.grant.api&&!radio_preference.grant.api&&!radio_control_grant.api);bridge_clean();
 puts(phase?"Actual BLE: fresh alias/tombstone and radio policy readers restore real-broker values PASS":"Actual BLE: alias/clear, failed physical enable with receipt-bound DELETE rollback, successful enable; six exact intents PASS");
}
