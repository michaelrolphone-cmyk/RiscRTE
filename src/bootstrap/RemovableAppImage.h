#pragma once
#include <RiscStorageVolumeV1.h>
#include <cstddef>
#include <cstdint>

namespace RiscBoot {
// No filesystem, board, or display implementation belongs in this reader.
// The native loader supplies an admitted volume and owns the returned snapshot.
struct RemovableAppImage {
  static constexpr size_t MaxBytes=2u*1024u*1024u;
  struct Hooks {
    void* (*allocate)(size_t);
    void (*free)(void*);
    bool (*checkpoint)(void*);
    void* context;
  };
  uint8_t* bytes=nullptr;
  size_t size=0;
  bool closeRetained=false;
  const char* error="SD app unavailable";
  bool read(const risc_storage_volume_api_v1* v,const char* path,Hooks h) {
    if(!v || v->api_version!=1 || v->struct_size<sizeof(*v) || !v->refresh || !v->ready ||
       !v->file_open_read || !v->file_read || !v->file_close || !h.allocate || !h.free || !h.checkpoint ||
       !v->refresh(v->context) || !v->ready(v->context))return false;
    uint64_t length=0;
    auto file=v->file_open_read(v->context,path,&length);
    if(!file){error="SD app file missing or busy";return false;}
    bool ok=false;
    if(length<52 || length>MaxBytes)error="SD app image size invalid";
    else if(!(bytes=static_cast<uint8_t*>(h.allocate(size_t(length)))))error="SD app snapshot allocation failed";
    else {
      size_t offset=0;
      ok=h.checkpoint(h.context);
      while(ok && offset<size_t(length)) {
        const size_t remaining=size_t(length)-offset;
        const size_t chunk=remaining<4096?remaining:4096;
        const size_t got=v->file_read(v->context,file,bytes+offset,chunk);
        if(!got || got>chunk){ok=false;break;}
        offset+=got;
        // A slow/short-reading source cannot avoid the time/ownership check.
        ok=h.checkpoint(h.context);
      }
      if(ok)size=size_t(length);
      else error="SD app read interrupted";
    }
    // Never retry an uncertain close or release its provider underneath it.
    if(!v->file_close(v->context,file,false)) {
      closeRetained=true;ok=false;error="SD app close retained; restart required";
    }
    if(!ok){if(bytes)h.free(bytes);bytes=nullptr;size=0;}
    return ok;
  }
};
}
