#pragma once
#include <cstdlib>
#include <memory>
#include <new>
#include <type_traits>
#if defined(ESP_PLATFORM) && (defined(RISC_PAIRED_BANKS) || defined(RISC_RUNTIME_METADATA_PSRAM))
#include <esp_heap_caps.h>
#endif
namespace RiscBoot {
#ifdef RISC_METADATA_ALLOCATION_TEST
void* metadataTestAllocate(size_t);
#endif
/* Preserve the existing explicit PSRAM metadata policy on those targets.
 * Other ports allocate only the admitted count from ordinary RAM. No fallback
 * from a selected PSRAM allocation into scarce internal/DMA memory. */
template<class T> struct MetadataDelete {
 void operator()(T*p)const{static_assert(std::is_trivially_destructible<T>::value,"Metadata must not own nested resources");std::free(p);}
};
template<class T>using MetadataArray=std::unique_ptr<T[],MetadataDelete<T>>;
template<class T> MetadataArray<T> metadataArray(size_t count) {
 if(!count || count>SIZE_MAX/sizeof(T))return {};
#if defined(RISC_METADATA_ALLOCATION_TEST)
 void*memory=metadataTestAllocate(count*sizeof(T));
#elif defined(ESP_PLATFORM) && (defined(RISC_PAIRED_BANKS) || defined(RISC_RUNTIME_METADATA_PSRAM))
 void*memory=heap_caps_malloc(count*sizeof(T),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
 void*memory=std::malloc(count*sizeof(T));
#endif
 if(!memory)return {};
 T*items=static_cast<T*>(memory);for(size_t i=0;i<count;i++)new(items+i)T();
 return MetadataArray<T>(items);
}
}
