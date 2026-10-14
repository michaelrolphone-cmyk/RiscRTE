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
static unsigned bridge_live,bridge_acquires,bridge_releases;static bool bridge_atomic[16];
void native_attached_atomic_release(bool before);
static bool bridge_fixture_acquire(const char*,uint32_t,uint64_t,risc_runtime_capability_v1*);
static bool bridge_fixture_release(risc_runtime_capability_v1*);
static bool bridge_acquire(const char *name,uint32_t version,uint64_t id,risc_runtime_capability_v1 *out){
 if(strcmp(name,RISC_KEY_VALUE_ATOMIC_CAPABILITY)&&strcmp(name,RISC_KEY_VALUE_CAPABILITY))return bridge_fixture_acquire(name,version,id,out);
 assert(version==1&&id==1);if(!bridge_broker->acquire(name,version,id,out))return false;
 assert(out->api&&out->slot&&out->generation);unsigned i=0;while(i<16&&bridge_grants[i].api)++i;assert(i<16);bridge_grants[i]=*out;bridge_atomic[i]=!strcmp(name,RISC_KEY_VALUE_ATOMIC_CAPABILITY);++bridge_live;++bridge_acquires;return true;
}
static bool bridge_release(risc_runtime_capability_v1 *g){
 for(unsigned i=0;i<16;++i)if(bridge_grants[i].api==g->api&&bridge_grants[i].slot==g->slot&&bridge_grants[i].generation==g->generation){
  assert(bridge_live);if(bridge_atomic[i])native_attached_atomic_release(true);if(!bridge_broker->release(g))return false;if(bridge_atomic[i])native_attached_atomic_release(false);assert(!g->api&&!g->slot&&!g->generation);bridge_grants[i]=(risc_runtime_capability_v1){0};--bridge_live;++bridge_releases;return true;
 }
 return bridge_fixture_release(g);
}
static void bridge_yield(uint32_t ms){assert(ms==5);native_settings_yield();}
static const risc_runtime_api_v1 *utility_bridge_runtime_get_api(uint32_t version){return version==1?&bridge_runtime:NULL;}
#define risc_runtime_get_api utility_bridge_runtime_get_api
static void bridge_begin(const risc_runtime_api_v1 *runtime){assert(!bridge_live);bridge_broker=runtime;bridge_runtime=*runtime;bridge_runtime.acquire=bridge_acquire;bridge_runtime.release=bridge_release;bridge_runtime.yield_ms=bridge_yield;}
static void bridge_clean(void){assert(!bridge_live&&bridge_acquires==bridge_releases);}
