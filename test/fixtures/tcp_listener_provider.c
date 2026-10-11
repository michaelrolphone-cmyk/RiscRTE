#include <RiscProviderV2.h>
#include <RiscTcpListenerV1.h>
#include <assert.h>
#include <string.h>
#ifndef PROVIDER_INDEX
#define PROVIDER_INDEX 0
#endif
extern bool tcp_provider_started(unsigned,const risc_tcp_listener_v1*);
extern void tcp_provider_stopped(unsigned);
static bool start(const risc_provider_dependency_v1* deps,size_t count){
 assert(count==1 && !strcmp(deps[0].capability_id,RISC_TCP_LISTENER_CAPABILITY));
 return tcp_provider_started(PROVIDER_INDEX,deps[0].api);
}
static bool quiesce(void){return true;}
static void stop(void){tcp_provider_stopped(PROVIDER_INDEX);}
static const struct {uint32_t version,size;} api={1,sizeof(api)};
static const risc_driver_v2 driver={2,sizeof(driver),PROVIDER_INDEX?"tcp-second":"tcp-first",PROVIDER_INDEX?"test.tcp.second":"test.tcp.first",1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t version){return version==2?&driver:0;}
