/* Actual pinned Home automatic controller. Atomic and legacy read capabilities
 * are forwarded to the Runtime broker; compiled upstream doubles stay unused. */
#include SYSTEM_HOME_SOURCE
#include "runtime_native_extra_fixture.h"
static const risc_runtime_api_v1 *broker;
static risc_runtime_capability_v1 live_grants[16];
static unsigned live_count,real_acquires,real_releases;
static bool broker_acquire(const char *name,uint32_t version,uint64_t id,risc_runtime_capability_v1 *out){
 if(!strcmp(name,RISC_KEY_VALUE_ATOMIC_CAPABILITY)||!strcmp(name,RISC_KEY_VALUE_CAPABILITY)){
  assert(version==1&&id==1);if(!broker->acquire(name,version,id,out))return false;
  assert(out->api!=&atomic&&out->api!=&kv);unsigned i=0;while(i<16&&live_grants[i].api)++i;assert(i<16);live_grants[i]=*out;++live_count;++real_acquires;return true;
 }
 assert(!strcmp(name,RISC_REALTIME_CONTROL_CAPABILITY)&&version==1&&!id);return false;
}
static bool broker_release(risc_runtime_capability_v1 *grant){
 for(unsigned i=0;i<16;++i)if(live_grants[i].api==grant->api&&live_grants[i].slot==grant->slot&&live_grants[i].generation==grant->generation){
  assert(live_count);if(!broker->release(grant))return false;live_grants[i]=(risc_runtime_capability_v1){0};--live_count;++real_releases;return true;
 }
 assert(false);return false;
}
static void broker_yield(uint32_t ms){yield(ms);native_settings_yield();}
static risc_runtime_api_v1 admitted;
static uint64_t real_revision(void){
 risc_runtime_capability_v1 g={.struct_size=sizeof(g)};assert(broker_acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,1,1,&g));
 const risc_key_value_atomic_v1 *a=g.api;risc_key_value_atomic_state_v1 s={.struct_size=sizeof(s)};assert(!a->state(a->context,&s)&&broker_release(&g));return s.revision;
}
#if HOME_CONTROLLER == 1
static const char *const selected_keys[]={"battery_edge","sleep_idle","sleep_deep","brightness","quick_radio"};
#define EXPECTED_INTENTS 5
#define BOUNDARY PORTABLE_AUTOMATIC_LOW_BATTERY
#elif HOME_CONTROLLER == 2
static const char *const selected_keys[]={"sleep_mode","sleep_idle","sleep_deep","quick_radio","brightness","alert_dnd"};
#define EXPECTED_INTENTS 7
#define BOUNDARY PORTABLE_AUTOMATIC_HOME_RULES
#else
static const char *const selected_keys[]={"ctx_p0","sleep_mode","sleep_idle","sleep_deep","brightness","alert_dnd"};
#define EXPECTED_INTENTS 5
#define BOUNDARY PORTABLE_AUTOMATIC_HOME_PRESET
static portable_context_preset fixture_preset(void){
 portable_context_preset p;portable_context_preset_init(&p);p.enabled=true;p.source=1;p.slot=2;strcpy(p.name,"Study");
 p.actions=PORTABLE_CONTEXT_SLEEP|PORTABLE_CONTEXT_IDLE|PORTABLE_CONTEXT_DEEP|PORTABLE_CONTEXT_BRIGHTNESS|PORTABLE_CONTEXT_DND;
 p.idle_ms=45000;p.deep_ms=180000;p.brightness=60;p.dnd=true;return p;
}
const char*native_extra_seed(unsigned index,const void**data,unsigned*size){
 if(index)return NULL;static uint8_t bytes[64];portable_context_preset p=fixture_preset();assert(portable_context_preset_encode(&p,bytes));*data=bytes;*size=sizeof(bytes);return "ctx_p0";
}
#endif
const char *native_extra_key(unsigned i){return i<sizeof(selected_keys)/sizeof(*selected_keys)?selected_keys[i]:NULL;}
unsigned native_extra_intents(void){return EXPECTED_INTENTS;}
void native_extra_intent(unsigned index,const void *bytes,unsigned size){
#if HOME_CONTROLLER == 1
 const char *key=selected_keys[index];
#elif HOME_CONTROLLER == 2
 static const char *const order[]={"sleep_mode","sleep_idle","sleep_deep","quick_radio","quick_radio","brightness","alert_dnd"};const char *key=order[index];
#else
 const char *key=selected_keys[index+1];
#endif
 const uint8_t *intent=bytes;assert(index<EXPECTED_INTENTS&&size==176&&!memcmp(intent,"PFA0001",8)&&intent[8]==PORTABLE_PREFERENCE_AUTOMATIC&&intent[11]==BOUNDARY);
 assert(!strcmp((const char*)intent+12,key)&&word(intent+172)==portable_preference_crc(intent,172));
}
void runtime_native_settings_preferences(const risc_key_value_v1 *view,unsigned ignored){
 (void)ignored;unsigned value=99;uint32_t ms=0;bool enabled=false;uint8_t flags=0;
#if HOME_CONTROLLER == 1
 assert(portable_low_battery_load(view,&enabled)&&enabled);
 assert(portable_sleep_timer_load(view,false,&ms)==PORTABLE_SLEEP_LOADED&&ms==20000);
 assert(portable_sleep_timer_load(view,true,&ms)==PORTABLE_SLEEP_LOADED&&ms==60000);
 assert(pqa_preference_load(view,PQA_BRIGHTNESS_KEY,40,0,&value)&&value==15);
 assert(portable_radio_load(view,&flags)&&flags==0);
#elif HOME_CONTROLLER == 2
 unsigned mode;assert(portable_sleep_load(view,&mode)==PORTABLE_SLEEP_LOADED&&mode==PORTABLE_SLEEP_HYBRID);
 assert(portable_sleep_timer_load(view,false,&ms)==PORTABLE_SLEEP_LOADED&&ms==30000);
 assert(portable_sleep_timer_load(view,true,&ms)==PORTABLE_SLEEP_LOADED&&ms==600000);
 assert(pqa_preference_load(view,PQA_BRIGHTNESS_KEY,40,0,&value)&&value==70);
 assert(pqa_dnd_load(view,&enabled)&&enabled);assert(portable_radio_load(view,&flags)&&flags==3);
#else
 (void)flags;unsigned mode;portable_context_preset actual,expected=fixture_preset();uint8_t a[64],b[64];
 assert(portable_context_preset_load(view,0,&actual)==RISC_KEY_VALUE_OK&&portable_context_preset_encode(&actual,a)&&portable_context_preset_encode(&expected,b)&&!memcmp(a,b,64));
 assert(portable_sleep_load(view,&mode)==PORTABLE_SLEEP_LOADED&&mode==expected.sleep);
 assert(portable_sleep_timer_load(view,false,&ms)==PORTABLE_SLEEP_LOADED&&ms==45000);
 assert(portable_sleep_timer_load(view,true,&ms)==PORTABLE_SLEEP_LOADED&&ms==180000);
 assert(pqa_preference_load(view,PQA_BRIGHTNESS_KEY,40,0,&value)&&value==60);
 assert(pqa_dnd_load(view,&enabled)&&enabled);
#endif
}
static bool selected_fingerprint(void *unused,uint32_t op,void *request){
 if(op!=CONTEXTS_FP_EVALUATE)return fingerprint(unused,op,request);
 contexts_evaluate_v1 *q=request;assert(q->observation.enabled);if(evaluated)return true;evaluated=true;
 const int values[CR_SETTINGS]={CR_KEEP,0,30,600,1,1,7,CR_KEEP,CR_KEEP,1};
 for(unsigned i=0;i<CR_SETTINGS;++i){q->result.settings[i]=values[i];q->result.winners[i]=(int8_t)(i%2);if(values[i]!=CR_KEEP)q->result.changed|=1u<<i;}
 return true;
}
void runtime_native_settings_controller(const risc_runtime_api_v1 *runtime,unsigned phase,const char *scenario){
 (void)scenario;broker=runtime;assert(!live_count);test="runtime-home";
 admitted=*broker;admitted.acquire=broker_acquire;admitted.release=broker_release;admitted.yield_ms=broker_yield;rt=&admitted;
 const uint64_t before=real_revision();
#if HOME_CONTROLLER == 1
 low_battery=(portable_low_battery){0};assert(observe(9)==(phase?PORTABLE_LOW_BATTERY_NONE:PORTABLE_LOW_BATTERY_ENTERED));
 const uint64_t after=real_revision();assert(after==before+(phase?0:EXPECTED_INTENTS));assert(!observe(8)&&real_revision()==after);
#elif HOME_CONTROLLER == 2
 if(!phase){
  static contexts_fingerprint_service_v1 selected;selected=fp;selected.fingerprint=selected_fingerprint;
  contexts_client=(portable_contexts_client){.runtime=rt,.api=&selected.base,.rules_available=true,.settings_valid=true,.policy={.enabled=true}};
  assert(contexts_rules_tick()&&rule_reports==2&&workflow_claims==1&&reported[0]==CR_APPLIED&&reported[1]==CR_APPLIED);
  assert(real_revision()==before+EXPECTED_INTENTS);now+=600;assert(contexts_rules_tick()&&real_revision()==before+EXPECTED_INTENTS&&rule_reports==2);
 }
#else
 if(!phase){
  room_status.audio=(contexts_source_status_v1){.source=1,.model_generation=3,.room_slot=2,.room_valid=true,.current=true,.room_entry=1};strcpy(room_status.audio.room_name,"Study");
  portable_contexts_client c={.runtime=rt,.api=&service,.settings_valid=true,.policy={.enabled=true},.loaded_at=10};uint16_t applied=0;
  assert(portable_contexts_apply_room_using(&c,&applied,face,home_automatic_preset));
  assert(claims==1&&reports==1&&applied==fixture_preset().actions&&preset_result==CONTEXTS_PRESET_APPLIED&&real_revision()==before+EXPECTED_INTENTS);
  ++c.loaded_at;assert(portable_contexts_apply_room_using(&c,&applied,face,home_automatic_preset)&&claims==1&&reports==1&&real_revision()==before+EXPECTED_INTENTS);
 }
#endif
 native_extra_verify_reads(broker);
 assert(!submits&&!collects&&!count&&!legacy_writes&&!acquires&&!releases&&!live_count&&real_acquires==real_releases&&!home_automatic_owned()&&!native_custody_retained);
 puts(phase?"Actual Home automatic: restored production readers and no low-edge replay PASS":"Actual Home automatic: real-broker PFA boundary and no repeated observation replay PASS");
}
