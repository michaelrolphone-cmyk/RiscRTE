#pragma once
#include "RiscMemoryHeapV1.h"
#include "RiscRandomV1.h"
#include <esp_heap_caps.h>
#include <esp_system.h>
#if RISC_NATIVE_ESP_IDF
#include <esp_random.h>
#endif
// Runtime validates explicit grants, owner task and invocation before dispatch.
namespace RiscCpu {
inline int32_t randomBytes(void* bytes,uint32_t length) {
 if(!bytes||!length||length>RISC_RANDOM_MAX_BYTES)return RISC_RANDOM_INVALID;
 esp_fill_random(bytes,length);return RISC_RANDOM_OK;
}
inline int32_t memoryHeap(risc_memory_heap_snapshot_v1* out) {
 if(!out||out->struct_size<sizeof(*out))return RISC_MEMORY_INVALID;
 uint32_t caps=MALLOC_CAP_8BIT;
 switch(out->memory_class) {
 case RISC_MEMORY_DEFAULT:break;
 case RISC_MEMORY_INTERNAL:caps|=MALLOC_CAP_INTERNAL;break;
 case RISC_MEMORY_EXTERNAL:caps|=MALLOC_CAP_SPIRAM;break;
 case RISC_MEMORY_DMA:caps|=MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL;break;
 default:return RISC_MEMORY_INVALID;
 }
 multi_heap_info_t info{};heap_caps_get_info(&info,caps);
 out->total_bytes=info.total_free_bytes+info.total_allocated_bytes;
 if(!out->total_bytes)return RISC_MEMORY_UNSUPPORTED;
 out->free_bytes=info.total_free_bytes;out->largest_block=info.largest_free_block;
 return RISC_MEMORY_OK;
}
}
