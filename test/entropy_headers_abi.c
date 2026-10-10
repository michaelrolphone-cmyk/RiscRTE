#include <RiscEntropyV1.h>
#include <stddef.h>
_Static_assert(RISC_ENTROPY_API_V1==1u,"entropy version");
_Static_assert(RISC_ENTROPY_BYTES_MAX==32u,"bounded entropy");
_Static_assert(offsetof(risc_entropy_v1,context)==8,"versioned prefix");
static int32_t fill(void* context,void* bytes,uint32_t size){
 (void)context;(void)bytes;(void)size;return RISC_ENTROPY_UNAVAILABLE;
}
const risc_entropy_v1 entropy_c_abi={1,sizeof(risc_entropy_v1),NULL,fill};
