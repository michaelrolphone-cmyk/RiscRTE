#pragma once
#include "NativeProviderPolicyV1.h"
#include <cstdlib>
#include <new>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace RuntimeProviders {
// Captures the complete trusted selection, including policies for providers not
// currently selected by boot.json. Later cohort validation inherits this copy,
// never the caller's potentially changed arrays. No allocation for empty sets.
struct NativeProviderPolicySnapshotV1 final {
  struct Row {
    char path[193]{}, id[64]{}, version[32]{}, capability[96]{};
    char importNames[128][128]{};
    const char* imports[128]{};
    char requirementNames[16][96]{};
    NativeProviderRequirementV1 requirements[16]{};
  };
  NativeProviderPolicyV1 entries[16]{};
  Row* rows=nullptr;
  size_t count=0;
  static void* allocate(size_t bytes) {
#ifdef ESP_PLATFORM
    void* p=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    return p?p:heap_caps_malloc(bytes,MALLOC_CAP_8BIT);
#else
    return std::malloc(bytes);
#endif
  }
  static void release(void* p) {
#ifdef ESP_PLATFORM
    heap_caps_free(p);
#else
    std::free(p);
#endif
  }
  static bool copy(char* out,size_t capacity,const char* from) {
    if(!nativePolicyName(from,capacity))return false;
    std::memcpy(out,from,std::strlen(from)+1);return true;
  }
  NativeProviderPolicySetV1 view() const { return {entries,count}; }
  static void destroy(NativeProviderPolicySnapshotV1* p) {
    if(!p)return;
    for(size_t i=0;i<p->count;++i)p->rows[i].~Row();
    release(p->rows);p->~NativeProviderPolicySnapshotV1();release(p);
  }
  static NativeProviderPolicySnapshotV1* capture(const NativeProviderPolicySetV1& source) {
    if(!source.count || !nativeProviderPolicySetValid(source))return nullptr;
    auto* memory=allocate(sizeof(NativeProviderPolicySnapshotV1));
    if(!memory)return nullptr;
    auto* result=new(memory) NativeProviderPolicySnapshotV1();
    result->rows=static_cast<Row*>(allocate(source.count*sizeof(Row)));
    if(!result->rows){destroy(result);return nullptr;}
    for(size_t i=0;i<source.count;++i){new(&result->rows[i]) Row();++result->count;}
    for(size_t i=0;i<result->count;++i) {
      const auto& from=source.entries[i];auto& p=result->entries[i];auto& row=result->rows[i];p=from;
      bool ok=copy(row.path,sizeof(row.path),from.relativeElfPath) &&
          copy(row.id,sizeof(row.id),from.driverId) && copy(row.version,sizeof(row.version),from.version) &&
          copy(row.capability,sizeof(row.capability),from.capability);
      p.relativeElfPath=row.path;p.driverId=row.id;p.version=row.version;p.capability=row.capability;
      for(size_t j=0;ok && j<p.importCount;++j) {
        ok=copy(row.importNames[j],sizeof(row.importNames[j]),from.imports[j]);
        row.imports[j]=row.importNames[j];
      }
      p.imports=row.imports;
      for(size_t j=0;ok && j<p.requirementCount;++j) {
        ok=copy(row.requirementNames[j],sizeof(row.requirementNames[j]),from.requirements[j].capability);
        row.requirements[j]={row.requirementNames[j],from.requirements[j].api};
      }
      p.requirements=p.requirementCount?row.requirements:nullptr;
      if(!ok){destroy(result);return nullptr;}
    }
    if(!nativeProviderPolicySetValid(result->view())){destroy(result);return nullptr;}
    return result;
  }
};
}
