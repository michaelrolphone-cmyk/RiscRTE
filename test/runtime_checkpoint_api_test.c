#include <RiscRuntimeCheckpointV1.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static unsigned calls;
static int32_t checkpoint(void){++calls;return RISC_RUNTIME_CHECKPOINT_BUSY;}
int main(void){
 const size_t prefix=offsetof(risc_runtime_api_v1,service_checkpoint);
 assert(prefix==RISC_RUNTIME_INITIAL_DEFAULT_V1_SIZE);
 void* old=calloc(1,prefix);assert(old);
 const uint32_t header[2]={1,(uint32_t)prefix};memcpy(old,header,sizeof(header));
 assert(risc_runtime_service_checkpoint_v1(old)==RISC_RUNTIME_CHECKPOINT_OK);free(old);
 risc_runtime_api_v1 api={0};api.api_version=1;api.struct_size=sizeof(api);
 assert(risc_runtime_service_checkpoint_v1(&api)==RISC_RUNTIME_CHECKPOINT_OK);
 api.service_checkpoint=checkpoint;api.struct_size=RISC_RUNTIME_SERVICE_CHECKPOINT_V1_SIZE-1;
 assert(risc_runtime_service_checkpoint_v1(&api)==RISC_RUNTIME_CHECKPOINT_OK && !calls);
 api.struct_size=sizeof(api);
 assert(risc_runtime_service_checkpoint_v1(&api)==RISC_RUNTIME_CHECKPOINT_BUSY && calls==1);
 api.api_version=2;assert(risc_runtime_service_checkpoint_v1(&api)==RISC_RUNTIME_CHECKPOINT_CONTEXT);
 assert(risc_runtime_service_checkpoint_v1(NULL)==RISC_RUNTIME_CHECKPOINT_CONTEXT);
 return 0;
}
