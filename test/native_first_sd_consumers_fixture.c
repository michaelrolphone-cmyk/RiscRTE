/* Each seed is created by the real provider in a separate process. */
#include "native_sd_consumers_fixture.c"
static unsigned first_collections,first_fault,first_faults,first_mutations_at_fault;
static int32_t first_collect(void*c,uint64_t s,uint64_t t,risc_storage_async_result_v1*r){
 int32_t rc=traced_collect(c,s,t,r);
 if(rc==RISC_STORAGE_ASYNC_OK&&++first_collections==first_fault){++first_faults;first_mutations_at_fault=mutations;r->status=RISC_STORAGE_ASYNC_IO;return RISC_STORAGE_ASYNC_IO;}
 return rc;
}
void first_trace_enable(unsigned fault){consumers_trace_enable();first_fault=fault;asynchronous_api.collect=first_collect;}
void first_seed_begin(unsigned parents){assert(!async_adapter.session&&!mounted&&!native_opened);assert(refresh(NULL)&&mounted);if(parents){assert(mkdir_path(NULL,"/RTECFG"));if(parents>1)assert(mkdir_path(NULL,"/RTECFG/00000003"));}}
unsigned first_collected(void){return first_collections;}
unsigned first_fault_count(void){return first_faults;}
unsigned first_fault_mutations(void){return first_mutations_at_fault;}
