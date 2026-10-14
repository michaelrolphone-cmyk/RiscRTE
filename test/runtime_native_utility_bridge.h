#pragma once
/* Test-only owner adapter. Storage tables are returned unchanged by the actual
 * Runtime broker. There is deliberately no fake atomic table or KV store here. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "runtime_native_extra_fixture.h"
#include "PortablePreferenceAtomic.h"
static const risc_runtime_api_v1 *bridge_broker;
static risc_runtime_api_v1 bridge_runtime;
static risc_runtime_capability_v1 bridge_grants[16];
static unsigned bridge_live,bridge_acquires,bridge_releases;
static bool bridge_fixture_acquire(const char*,uint32_t,uint64_t,risc_runtime_capability_v1*);
static bool bridge_fixture_release(risc_runtime_capability_v1*);
static bool bridge_acquire(const char *name,uint32_t version,uint64_t id,risc_runtime_capability_v1 *out){
 if(strcmp(name,RISC_KEY_VALUE_ATOMIC_CAPABILITY)&&strcmp(name,RISC_KEY_VALUE_CAPABILITY))return bridge_fixture_acquire(name,version,id,out);
 assert(version==1&&id==1);if(!bridge_broker->acquire(name,version,id,out))return false;
 assert(out->api&&out->slot&&out->generation);unsigned i=0;while(i<16&&bridge_grants[i].api)++i;assert(i<16);bridge_grants[i]=*out;++bridge_live;++bridge_acquires;return true;
}
static bool bridge_release(risc_runtime_capability_v1 *g){
 for(unsigned i=0;i<16;++i)if(bridge_grants[i].api==g->api&&bridge_grants[i].slot==g->slot&&bridge_grants[i].generation==g->generation){
  assert(bridge_live);if(!bridge_broker->release(g))return false;assert(!g->api&&!g->slot&&!g->generation);bridge_grants[i]=(risc_runtime_capability_v1){0};--bridge_live;++bridge_releases;return true;
 }
 return bridge_fixture_release(g);
}
static void bridge_yield(uint32_t ms){assert(ms==5);native_settings_yield();}
static const risc_runtime_api_v1 *utility_bridge_runtime_get_api(uint32_t version){return version==1?&bridge_runtime:NULL;}
#define risc_runtime_get_api utility_bridge_runtime_get_api
static void bridge_begin(const risc_runtime_api_v1 *runtime){assert(!bridge_live);bridge_broker=runtime;bridge_runtime=*runtime;bridge_runtime.acquire=bridge_acquire;bridge_runtime.release=bridge_release;bridge_runtime.yield_ms=bridge_yield;}
static void bridge_clean(void){assert(!bridge_live&&bridge_acquires==bridge_releases);}
static uint64_t bridge_revision(void){risc_runtime_capability_v1 g={.struct_size=sizeof(g)};assert(bridge_acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,1,1,&g));const risc_key_value_atomic_v1*a=g.api;risc_key_value_atomic_state_v1 s={.struct_size=sizeof(s)};assert(!a->state(a->context,&s)&&bridge_release(&g));return s.revision;}
static void bridge_word(uint8_t *p,uint32_t value){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(value>>(8*i));}
static void bridge_wide(uint8_t *p,uint64_t value){bridge_word(p,(uint32_t)value);bridge_word(p+4,(uint32_t)(value>>32));}
static uint8_t bridge_intents[12][192];static unsigned bridge_intent_sizes[12],bridge_intent_count;
static uint64_t bridge_planned_revision;
static struct {char key[16];uint8_t bytes[64];uint32_t size;bool present;} bridge_prior[8];
static unsigned bridge_prior_count;
static void bridge_plan_begin(void){bridge_intent_count=bridge_prior_count=0;bridge_planned_revision=bridge_revision();memset(bridge_intents,0,sizeof(bridge_intents));}
static unsigned bridge_prior_index(const char *key){
 for(unsigned i=0;i<bridge_prior_count;++i)if(!strcmp(bridge_prior[i].key,key))return i;
 unsigned i=bridge_prior_count++;assert(i<8);memset(&bridge_prior[i],0,sizeof(bridge_prior[i]));strcpy(bridge_prior[i].key,key);
 risc_runtime_capability_v1 g={.struct_size=sizeof(g)};assert(bridge_acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,1,1,&g));const risc_key_value_atomic_v1*a=g.api;
 risc_key_value_atomic_state_v1 s={.struct_size=sizeof(s)};assert(!a->state(a->context,&s));int rc=a->get(a->context,s.revision,key,bridge_prior[i].bytes,64,&bridge_prior[i].size);
 assert(rc==RISC_ATOMIC_OK||rc==RISC_ATOMIC_NOT_FOUND);bridge_prior[i].present=rc==RISC_ATOMIC_OK;assert(bridge_prior[i].present||!bridge_prior[i].size);assert(bridge_release(&g));return i;
}
static unsigned bridge_expect_put(const char *key,const void *desired,unsigned size,unsigned boundary){
 unsigned i=bridge_prior_index(key),n=bridge_intent_count++;assert(n<12&&size&&size<=64);uint8_t*p=bridge_intents[n];
 memcpy(p,boundary?"PFA0001":"PFI0001",8);p[8]=boundary?PORTABLE_PREFERENCE_AUTOMATIC:PORTABLE_PREFERENCE_USER;p[9]=RISC_ATOMIC_PUT;p[10]=bridge_prior[i].present;p[11]=(uint8_t)boundary;
 strcpy((char*)p+12,key);bridge_word(p+28,bridge_prior[i].size);bridge_word(p+32,size);bridge_wide(p+36,bridge_planned_revision++);
 memcpy(p+44,bridge_prior[i].bytes,bridge_prior[i].size);memcpy(p+108,desired,size);bridge_word(p+172,portable_preference_crc(p,172));bridge_intent_sizes[n]=176;
 memset(bridge_prior[i].bytes,0,64);memcpy(bridge_prior[i].bytes,desired,size);bridge_prior[i].size=size;bridge_prior[i].present=true;return n;
}
static void bridge_expect_rollback(unsigned origin,const risc_key_value_atomic_receipt_v1 *receipt){
 assert(origin<bridge_intent_count&&receipt->status==RISC_ATOMIC_OK&&receipt->operation_id&&receipt->revision==bridge_planned_revision);
 uint8_t*from=bridge_intents[origin];unsigned n=bridge_intent_count++;assert(n<12);uint8_t*p=bridge_intents[n];
 memcpy(p,"PFR0001",8);p[8]=PORTABLE_PREFERENCE_ROLLBACK;p[9]=from[10]?RISC_ATOMIC_PUT:RISC_ATOMIC_DELETE;p[10]=1;memcpy(p+12,from+12,16);
 memcpy(p+28,from+32,4);memcpy(p+32,from+28,4);bridge_wide(p+36,bridge_planned_revision++);memcpy(p+44,from+108,64);memcpy(p+108,from+44,64);
 bridge_wide(p+172,receipt->operation_id);bridge_wide(p+180,receipt->revision);bridge_word(p+188,portable_preference_crc(p,188));bridge_intent_sizes[n]=192;
 unsigned i=bridge_prior_index((char*)from+12);bridge_prior[i].present=from[10]!=0;bridge_prior[i].size=from[28];memcpy(bridge_prior[i].bytes,from+44,64);
}
void native_extra_intent(unsigned index,const void *bytes,unsigned size){assert(index<bridge_intent_count&&size==bridge_intent_sizes[index]&&!memcmp(bytes,bridge_intents[index],size));}
static void bridge_plan_end(unsigned expected){assert(bridge_intent_count==expected&&bridge_revision()==bridge_planned_revision);}
