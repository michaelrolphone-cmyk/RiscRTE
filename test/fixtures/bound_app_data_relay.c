/* This provider has no file dependency/map. Its dependency performs the file I/O. */
#include <RiscProviderV2.h>
#include <assert.h>
#include <string.h>
extern void bound_files_event(unsigned,const char*);
extern bool bound_files_relay_result(void);
extern unsigned bound_files_relay_stage(void);
struct file_api {uint32_t version,size;void(*retained)(void);};
static const struct file_api* dependency;
static bool start(const risc_provider_dependency_v1* deps,size_t count){
 bound_files_event(2,"start");assert(count==1 && !strcmp(deps[0].capability_id,"test.files.first"));
 const struct file_api* api=deps[0].api;assert(api && api->version==1 && api->size==sizeof(*api));
 dependency=api;if(bound_files_relay_stage()==0)api->retained();return bound_files_relay_result();
}
static bool quiesce(void){bound_files_event(2,"quiesce");if(bound_files_relay_stage()==2)dependency->retained();return true;}
static void stop(void){bound_files_event(2,"stop");if(bound_files_relay_stage()==3)dependency->retained();}
static bool diagnostic(char* out,size_t cap){bound_files_event(2,"diagnostic");if(bound_files_relay_stage()==1)dependency->retained();assert(cap);out[0]=0;return true;}
static const uint32_t api[2]={1,sizeof(api)};
static const risc_driver_diagnostics_v2 descriptor={{2,sizeof(descriptor),"file-relay","test.files.relay",1,api,start,stop,quiesce},diagnostic};
__attribute__((constructor)) static void loaded(void){bound_files_event(2,"load");}
__attribute__((destructor)) static void unloaded(void){bound_files_event(2,"unload");}
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t version){return version==2?&descriptor.base:0;}
