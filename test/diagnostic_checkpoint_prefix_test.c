#include <RiscDiagnosticCheckpointV1.h>
#include <assert.h>
#include <stdlib.h>
int main(void){
 const size_t old_size=offsetof(risc_runtime_api_v1,diagnostic_checkpoint_client);
 risc_runtime_api_v1* old=(risc_runtime_api_v1*)calloc(1,old_size);assert(old);
 old->api_version=1;old->struct_size=(uint32_t)old_size;
 risc_diagnostic_checkpoint_client_v1 client={.struct_size=sizeof(client)};
 assert(!risc_runtime_diagnostic_checkpoint_client(old,&client));free(old);
 assert(!risc_runtime_diagnostic_checkpoint_client(NULL,&client));
 return 0;
}
