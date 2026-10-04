#include "ports/esp32s3/NativeRuntime.h"
#include <cassert>
#include <cstdlib>
#include <cstdio>
static unsigned calls=0;static bool fail=true;static void* allocation=nullptr;
void* heap_caps_malloc(size_t bytes,unsigned caps){
 ++calls;assert(bytes==sizeof(RiscBoot::Runtime));assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(fail)return nullptr;
 return allocation=std::malloc(bytes);
}
void heap_caps_free(void*){assert(false && "retained metadata must not be freed by creation");}
int main(){
 assert(RiscCpu::createRetainedRuntime({})==nullptr && calls==1);
 fail=false;auto* runtime=RiscCpu::createRetainedRuntime({});assert(runtime && runtime==allocation && calls==2);
 assert(!runtime->active() && !runtime->retained() && runtime->appCount()==0);
 // Deliberately no destructor: production retains manifest/grant pointers to
 // reset. This test releases raw storage only after all observations end.
 std::free(allocation);std::printf("Paired Runtime metadata: %zu bytes, PSRAM-only/OOM fail-closed, retained lifetime PASS\n",sizeof(*runtime));
}
