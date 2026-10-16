#pragma once
#include "RuntimeFiles.h"
#include <cerrno>
#include <cstddef>
#include <cstdint>

namespace RiscBoot {
// One caller-owned image allocation, filled directly by the selected provider.
// A successful read transfers bytes to the caller. Failed cleanup retains the
// session/token and allocation; no retry or alternate source is permitted.
struct RuntimeFileImage {
  struct Hooks {
    void* (*allocate)(size_t);
    void (*free)(void*);
    bool (*safe)(void*);
    bool (*checkpoint)(void*,size_t);
    void* context;
  };
  uint8_t* bytes=nullptr;
  size_t size=0;
  uint32_t handle=0;
  bool retained=false;
  int error=EIO;

  bool read(RuntimeFileView source,const char* path,Hooks hooks,
            size_t expected=0,size_t maximum=8u*1024u*1024u) {
    if(bytes || handle || !hooks.allocate || !hooks.free || !hooks.safe || !hooks.checkpoint) {
      error=EINVAL;return false;
    }
    bool dispatched=false;
    const auto safe=[&]{return !source.retained() && hooks.safe(hooks.context);};
    const auto checkpoint=[&](size_t n){
      if(!safe()){if(dispatched)source.retainCustody();error=EIO;return false;}
      const bool progressed=hooks.checkpoint(hooks.context,n);
      if(!safe()){if(dispatched)source.retainCustody();error=EIO;return false;}
      if(!progressed){error=ETIMEDOUT;return false;}
      return true;
    };
    if(!checkpoint(0)){retained=source.retained();return false;}
    const int32_t opened=source.open(path,&handle);dispatched=true;
    if(!safe())source.retainCustody();
    bool ok=opened==RISC_FILES_OK && !source.retained();
    if(!ok)error=opened==RISC_FILES_NOT_FOUND?ENOENT:EIO;
    uint64_t length=0,position=0;
    if(ok)ok=checkpoint(0) && source.info(handle,&length,&position)==RISC_FILES_OK;
    if(ok && (length<52 || length>maximum || position || (expected && length!=expected))) {
      error=EFBIG;ok=false;
    }
    if(ok){bytes=static_cast<uint8_t*>(hooks.allocate(size_t(length)));if(!bytes){error=ENOMEM;ok=false;}}
    size_t offset=0;
    while(ok && offset<size_t(length)) {
      uint32_t wanted=uint32_t(size_t(length)-offset),actual=0;
      if(wanted>4096)wanted=4096;
      if(wanted>RISC_STORAGE_FILES_IO_MAX)wanted=RISC_STORAGE_FILES_IO_MAX;
      ok=checkpoint(0) && source.read(handle,bytes+offset,wanted,&actual)==RISC_FILES_OK && actual && actual<=wanted;
      if(ok){offset+=actual;ok=checkpoint(actual);}
    }
    // Detect truncation, growth and inconsistent positions on this same open
    // object. These probes occur only on misses, never on an image-cache hit.
    if(ok){uint8_t extra=0;uint32_t actual=0;
      ok=checkpoint(0) && source.read(handle,&extra,1,&actual)==RISC_FILES_OK && !actual &&
         checkpoint(0) && source.info(handle,&length,&position)==RISC_FILES_OK &&
         length==offset && position==offset;
    }
    if(handle) {
      if(!safe()){source.retainCustody();retained=true;ok=false;}
      else if(source.close(handle)!=RISC_FILES_OK){retained=true;ok=false;}
      else {handle=0;if(!safe()){source.retainCustody();retained=true;ok=false;}}
    }
    retained=retained || source.retained();
    ok=ok && !retained && checkpoint(0);
    retained=retained || source.retained();
    ok=ok && !retained;
    if(ok){size=offset;error=0;return true;}
    if(retained)error=EIO; // Terminal custody must never advertise a retryable OOM.
    else {if(bytes)hooks.free(bytes);bytes=nullptr;}
    size=0;return false;
  }
};
}
