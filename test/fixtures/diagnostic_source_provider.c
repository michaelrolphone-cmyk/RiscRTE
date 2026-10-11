#include <RiscProviderV2.h>
#include <RiscDiagnosticSourceV1.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
extern void test_diagnostic_source_provider_started(void);
static bool start(const risc_provider_dependency_v1* deps,size_t count){
 assert(count==1 && deps && !strcmp(deps[0].capability_id,RISC_DIAGNOSTIC_SOURCE_CAPABILITY));
 assert(deps[0].api_version==1);
 const risc_diagnostic_source_api_v1* source=deps[0].api;
 assert(source && source->api_version==1 && source->struct_size==sizeof(*source) && source->read);
 char out[RISC_DIAGNOSTIC_SOURCE_TEXT_MAX];uint32_t written=0,revision=0;uint64_t sequence=99;
 assert(source->read(source->context,8,out,sizeof(out),&written,&sequence,&revision)==1);
 assert(!strcmp(out,"boot snapshot") && written==13 && !sequence && revision==3);
 test_diagnostic_source_provider_started();return true;
}
static bool quiesce(void){return true;}
static void stop(void){}
static const struct {uint32_t version,size;} api={1,sizeof(api)};
static const risc_driver_v2 descriptor={2,sizeof(descriptor),"diagnostic-consumer","test.diagnostic-consumer",1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t version){return version==2?&descriptor:0;}
