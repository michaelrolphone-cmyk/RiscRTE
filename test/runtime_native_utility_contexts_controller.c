/* Actual Contexts user controls or foreground rule application, real Runtime
 * storage. The detection-service observation is synthetic; its action mask,
 * ordering, codecs, atomic origins and result reporting are production code. */
#include "runtime_native_utility_bridge.h"
#include UTILITY_CONTEXTS_SOURCE
#ifndef UTILITY_CONTEXTS_AUTOMATIC
#define UTILITY_CONTEXTS_AUTOMATIC 0
#endif
const t5_app_manifest_t portable_catalog[]={{.file_name="calculator.elf",.display_name="CALCULATOR",.compatible=true}};
const unsigned portable_catalog_count=1;
static contexts_fingerprint_status_v1 observed={.struct_size=sizeof(observed),.sources=3,.room_slot=-1,.event_slot=-1};
static cr_result evaluated;static unsigned evaluations,reports,workflow_claims,reported_result;
static bool service_pause(void*c){(void)c;return true;}
static bool service_step(void*c,const contexts_policy_v1*p){(void)c;assert(p->struct_size==sizeof(*p));return true;}
static bool service_status(void*c,contexts_status_v1*q){(void)c;*q=(contexts_status_v1){.struct_size=sizeof(*q),.audio={.model_state=CONTEXTS_MODEL_READY},.radio={.model_state=CONTEXTS_MODEL_READY}};return true;}
static bool service_fingerprint(void*c,uint32_t op,void*p){
 (void)c;switch(op){
 case CONTEXTS_FP_STATUS:*(contexts_fingerprint_status_v1*)p=observed;return true;
 case CONTEXTS_FP_CONFIG:{contexts_fingerprint_config_v1*q=p;observed.sources=q->sources;observed.temporal_only=q->temporal_only;return true;}
 case CONTEXTS_FP_EVALUATE:{contexts_evaluate_v1*q=p;assert(q->observation.enabled);q->result=evaluated;memset(&evaluated,0,sizeof(evaluated));++evaluations;return true;}
 case CONTEXTS_FP_WORKFLOW:{contexts_workflow_v1*q=p;if(q->claim){++workflow_claims;q->available=false;}else{assert(q->rule==0&&q->result==CR_APPLIED);reported_result=q->result;++reports;}return true;}
 default:assert(!"unexpected detection-service operation");return false;
 }
}
static const contexts_fingerprint_service_v1 fixture_service={.base={.api_version=1,.struct_size=sizeof(fixture_service),.pause=service_pause,.step=service_step,.status=service_status},.fingerprint_abi=0x31504643u,.fingerprint=service_fingerprint};
static bool bridge_fixture_acquire(const char*n,uint32_t version,uint64_t id,risc_runtime_capability_v1*g){(void)g;assert(!id);assert((!strcmp(n,"rtc.clock")&&version==2)||(!strcmp(n,"time.realtime.control")&&version==1)||(!strcmp(n,"time.realtime")&&version==1));return false;}
static bool bridge_fixture_release(risc_runtime_capability_v1*g){(void)g;assert(false);return false;}
#if UTILITY_CONTEXTS_AUTOMATIC
static const char *const selected_keys[]={"sleep_idle","quick_radio","brightness","alert_dnd"};
#else
static const char *const selected_keys[]={"contexts_on","context_sources","context_timing"};
#endif
const char *native_extra_key(unsigned i){return i<sizeof(selected_keys)/sizeof(*selected_keys)?selected_keys[i]:NULL;}
unsigned native_extra_intents(void){return UTILITY_CONTEXTS_AUTOMATIC?5:3;}
void runtime_native_settings_preferences(const risc_key_value_v1 *view,unsigned ignored){
 (void)ignored;
#if UTILITY_CONTEXTS_AUTOMATIC
 uint32_t millis=0;unsigned brightness=0;bool dnd=false;uint8_t flags=0;
 assert(portable_sleep_timer_load(view,false,&millis)==PORTABLE_SLEEP_LOADED&&millis==30000);
 assert(portable_radio_load(view,&flags)&&flags==2);assert(pqa_preference_load(view,PQA_BRIGHTNESS_KEY,40,0,&brightness)&&brightness==70);assert(pqa_dnd_load(view,&dnd)&&dnd);
#else
 bool enabled=false;uint8_t value=0;uint32_t size=0;
 assert(portable_context_enabled_load(view,&enabled)&&enabled);
 assert(!view->get(view->context,"context_sources",&value,1,&size)&&size==1&&value==1);
 assert(!view->get(view->context,"context_timing",&value,1,&size)&&size==1&&value==1);
#endif
}
void runtime_native_settings_controller(const risc_runtime_api_v1 *rt,unsigned phase,const char *scenario){
 (void)scenario;bridge_begin(rt);ctx_runtime=&bridge_runtime;ctx_service=&fixture_service.base;cs_terminal=ctx_exit=false;
 cs_client=(portable_contexts_client){.runtime=ctx_runtime,.api=ctx_service,.fingerprint_sources=3};ctx_preference=(utility_preference_atomic){0};ctx_automatic=(portable_preference_automatic){0};ctx_automatic_grant=(risc_runtime_capability_v1){0};
 ctx_fp_valid=true;ctx_fp=observed;ctx_fp.sources=3;ctx_fp.temporal_only=false;cs_source_mask=3;ctx_enabled=false;cu_page=CU_HOME;
 if(!phase){bridge_plan_begin();
#if UTILITY_CONTEXTS_AUTOMATIC
  const uint8_t idle[]={0x54,1,30,0,(30^0xa5)},wifi_off[]={0x51,1,0,0xa5},ble_on[]={0x51,1,2,0xa7},brightness=70,dnd=1;
  bridge_expect_put("sleep_idle",idle,5,PORTABLE_AUTOMATIC_CONTEXTS_EDITOR);bridge_expect_put("quick_radio",wifi_off,4,PORTABLE_AUTOMATIC_CONTEXTS_EDITOR);bridge_expect_put("quick_radio",ble_on,4,PORTABLE_AUTOMATIC_CONTEXTS_EDITOR);bridge_expect_put("brightness",&brightness,1,PORTABLE_AUTOMATIC_CONTEXTS_EDITOR);bridge_expect_put("alert_dnd",&dnd,1,PORTABLE_AUTOMATIC_CONTEXTS_EDITOR);
  cs_client.rules_available=cs_client.settings_valid=ctx_enabled=true;
  for(unsigned f=0;f<CR_SETTINGS;++f){evaluated.settings[f]=CR_KEEP;evaluated.winners[f]=-1;}
  evaluated.changed=0x274u;evaluated.settings[CR_SLEEP]=30;evaluated.settings[CR_WIFI]=0;evaluated.settings[CR_BLUETOOTH]=1;evaluated.settings[CR_BRIGHTNESS]=7;evaluated.settings[CR_DND]=1;
  for(unsigned f=0;f<CR_SETTINGS;++f)if(evaluated.changed&(1u<<f))evaluated.winners[f]=0;
  assert(cs_rules_tick()&&evaluations==1&&reports==1&&reported_result==CR_APPLIED&&workflow_claims==1);bridge_plan_end(5);
  assert(cs_rules_tick()&&evaluations==2&&reports==1&&workflow_claims==2);bridge_plan_end(5);
#else
  const uint8_t enabled[]={'C',1,1,0xa4},one=1;
  bridge_expect_put("contexts_on",enabled,4,0);cu_action(CU_ROW_AUTO);assert(ctx_enabled);
  bridge_expect_put("context_sources",&one,1,0);cu_action(CU_ROW_SOURCE+1);
  bridge_expect_put("context_timing",&one,1,0);cu_action(CU_ROW_SOURCE+2);bridge_plan_end(3);
#endif
 }
 native_extra_verify_reads(bridge_broker);
#if !UTILITY_CONTEXTS_AUTOMATIC
 /* Fresh shared policy reader, with all namespace-1 gets brokered by Runtime. */
 portable_contexts_client fresh={.api=ctx_service,.runtime=ctx_runtime,.fingerprint_sources=3};assert(portable_contexts_step(&fresh,true,true)&&fresh.loaded&&fresh.settings_valid&&fresh.policy.enabled&&fresh.policy.sources==1&&fresh.fingerprint_temporal_only&&!fresh.storage.api);
#endif
 assert(!cs_terminal&&!ctx_preference.grant.api&&!ctx_preference.edit.token&&!ctx_automatic_grant.api&&!ctx_automatic.operation.token&&!ctx_automatic.running&&!ctx_automatic.retained);bridge_clean();
 puts(phase?"Actual Contexts: fresh production preference readers restore real-broker values PASS":UTILITY_CONTEXTS_AUTOMATIC?"Actual Contexts automatic: five exact PFA boundary-4 intents, original 0x274 order, applied report and no repeated evaluation writes PASS":"Actual Contexts user controls: enabled/sources/timing, three exact PFI intents PASS");
}
