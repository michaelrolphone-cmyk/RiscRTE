#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <stdlib.h>
#define RISC_APP_IMAGE_CACHE 1
static unsigned calls, reclaims;
static bool reclaimResult;
static void* results[2];
static void* expectedPointer;
static size_t expectedSize;
static void* checked_realloc(void* pointer,size_t size) {
  assert(pointer==expectedPointer && size==expectedSize && calls<2);
  return results[calls++];
}
#define realloc checked_realloc
#include "../lib/hal/RuntimeImagePressure.h"
#undef realloc
extern "C" bool risc_runtime_reclaim_app_images() {++reclaims;return reclaimResult;}
int main() {
  char original[8]{}, replacement[16]{};
  auto check=[&](void* pointer,size_t size,void* first,bool reclaimed,void* second,void* expected,unsigned count,unsigned pressure) {
    expectedPointer=pointer;expectedSize=size;results[0]=first;results[1]=second;
    calls=reclaims=0;reclaimResult=reclaimed;
    assert(risc_image_realloc(pointer,size)==expected && calls==count && reclaims==pressure);
  };
  check(original,0,nullptr,true,replacement,nullptr,1,0);
  check(original,0,replacement,true,nullptr,replacement,1,0);
  check(original,16,replacement,true,nullptr,replacement,1,0);
  check(original,16,nullptr,false,replacement,nullptr,1,1);
  check(original,16,nullptr,true,replacement,replacement,2,1);
  check(original,16,nullptr,true,nullptr,nullptr,2,1);
  check(nullptr,16,nullptr,true,replacement,replacement,2,1);
}
