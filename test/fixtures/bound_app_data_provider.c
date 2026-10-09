#include <RiscProviderV2.h>
#include <RiscBoundAppDataV1.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#ifndef PROVIDER_INDEX
#define PROVIDER_INDEX 0
#endif
_Static_assert(sizeof(risc_bound_app_data_v1)==sizeof(risc_app_data_v1), "copied app-data ABI");
_Static_assert(offsetof(risc_bound_app_data_v1,context)==8, "context prefix");
extern bool bound_files_started(unsigned,risc_bound_app_data_v1);
extern void bound_files_event(unsigned,const char*);
extern void bound_files_denied(risc_bound_app_data_v1);
static risc_bound_app_data_v1 storage;
static bool start(const risc_provider_dependency_v1* deps,size_t count){
 bound_files_event(PROVIDER_INDEX,"start");
 assert(count==1 && deps && !strcmp(deps[0].capability_id,RISC_BOUND_APP_DATA_CAPABILITY));
 assert(deps[0].api_version==1 && deps[0].api);
 storage=*(const risc_bound_app_data_v1*)deps[0].api;
 assert(storage.api_version==1 && storage.struct_size==sizeof(storage) && storage.context);
 return bound_files_started(PROVIDER_INDEX,storage);
}
static bool quiesce(void){bound_files_event(PROVIDER_INDEX,"quiesce");bound_files_denied(storage);return true;}
static void stop(void){bound_files_event(PROVIDER_INDEX,"stop");bound_files_denied(storage);}
static bool diagnostic(char* out,size_t cap){
 bound_files_event(PROVIDER_INDEX,"diagnostic");bound_files_denied(storage);
 assert(cap>1);out[0]='x';out[1]=0;return true;
}
extern void bound_files_indirect(void);
struct file_api {uint32_t version,size;void(*retained)(void);};
static const struct file_api api={1,sizeof(api),bound_files_indirect};
extern void bound_files_cooperative(unsigned,bool);
static void poll(uint32_t budget){assert(budget);bound_files_cooperative(PROVIDER_INDEX,false);}
static void service(uint32_t budget){assert(budget);bound_files_cooperative(PROVIDER_INDEX,true);}
static const risc_driver_service_v2 descriptor={{{{2,sizeof(descriptor),
 PROVIDER_INDEX?"file-second":"file-first",PROVIDER_INDEX?"test.files.second":"test.files.first",1,&api,start,stop,quiesce},diagnostic,0},poll},
 RISC_DRIVER_SERVICE_TAG_V1,RISC_DRIVER_SERVICE_VERSION_V1,service};
__attribute__((constructor)) static void loaded(void){bound_files_event(PROVIDER_INDEX,"load");}
__attribute__((destructor)) static void unloaded(void){bound_files_event(PROVIDER_INDEX,"unload");}
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t version){return version==2?&descriptor.poll.streams.driver:0;}
