#pragma once
#include "RuntimeFiles.h"
#include "runtime/storage/ImageEnvelope.h"
#include "runtime/storage/Lz4Block.h"
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
    bool (*observe)(void*,const uint8_t*,size_t)=nullptr; // physical bytes, not decoded bytes
  };
  uint8_t* bytes=nullptr;
  size_t size=0,physicalSize=0;
  bool encoded=false;
  uint32_t handle=0;
  bool retained=false;
  int error=EIO;

  bool read(RuntimeFileView source,const char* path,Hooks hooks,
            size_t expected=0,size_t maximum=8u*1024u*1024u,bool allowEnvelope=false) {
    if(allowEnvelope)return readRepresented(source,path,hooks,expected,maximum);
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
      if(ok && !safe()){source.retainCustody();ok=false;}
      if(ok && hooks.observe){ok=hooks.observe(hooks.context,bytes+offset,actual);if(!safe()){source.retainCustody();ok=false;}}
      if(ok){offset+=actual;ok=checkpoint(actual);}
    }
    if(ok && RiscStorage::ImageEnvelope::magic(bytes,offset)){error=ENOEXEC;ok=false;} // explicit raw-only/provider refusal
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
    if(ok){size=physicalSize=offset;error=0;return true;}
    if(retained)error=EIO; // Terminal custody must never advertise a retryable OOM.
    else {if(bytes)hooks.free(bytes);bytes=nullptr;}
    size=0;return false;
  }
 private:
  bool readRepresented(RuntimeFileView source,const char* path,Hooks hooks,size_t expected,size_t maximum) {
    if(bytes||handle||!hooks.allocate||!hooks.free||!hooks.safe||!hooks.checkpoint){error=EINVAL;return false;}
    using Envelope=RiscStorage::ImageEnvelope;
    using Decoder=RiscStorage::Lz4Block;
    bool dispatched=false;
    const auto safe=[&](){return !source.retained()&&hooks.safe(hooks.context);};
    const auto checkpoint=[&](size_t n){
      if(!safe()){if(dispatched)source.retainCustody();error=EIO;return false;}
      const bool progress=hooks.checkpoint(hooks.context,n);
      if(!safe()){if(dispatched)source.retainCustody();error=EIO;return false;}
      if(!progress){error=ETIMEDOUT;return false;}return true;
    };
    const auto read=[&](uint8_t* out,uint32_t wanted,uint32_t& actual){
      actual=0;if(!checkpoint(0))return false;
      const int32_t rc=source.read(handle,out,wanted,&actual);
      if(!safe()){source.retainCustody();error=EIO;return false;}
      if(rc!=RISC_FILES_OK||actual>wanted){error=EIO;return false;}
      if(actual&&hooks.observe){
        const bool observed=hooks.observe(hooks.context,out,actual);
        if(!safe()){source.retainCustody();error=EIO;return false;}
        if(!observed){error=EIO;return false;}
      }
      return checkpoint(actual);
    };
    if(!checkpoint(0)){retained=source.retained();return false;}
    const int32_t opened=source.open(path,&handle);dispatched=true;
    if(!safe())source.retainCustody();
    bool ok=opened==RISC_FILES_OK&&!source.retained();
    if(!ok)error=opened==RISC_FILES_NOT_FOUND?ENOENT:EIO;
    uint64_t length=0,position=0;
    if(ok){ok=checkpoint(0)&&source.info(handle,&length,&position)==RISC_FILES_OK;if(!safe()){source.retainCustody();ok=false;}}
    const uint64_t limit=maximum>UINT32_MAX?UINT32_MAX:maximum;
    if(ok&&(length<Envelope::HeaderBytes||length>limit+limit/255u+48u||position||(expected&&length!=expected))){error=EFBIG;ok=false;}
    uint8_t header[Envelope::HeaderBytes];size_t offset=0,logical=0;
    while(ok&&offset<sizeof(header)){
      uint32_t actual=0;ok=read(header+offset,uint32_t(sizeof(header)-offset),actual)&&actual;
      if(ok)offset+=actual;
    }
    Envelope envelope;
    if(ok){
      encoded=Envelope::magic(header,sizeof(header));
      if(encoded){ok=envelope.parse(header,sizeof(header),length,maximum);logical=envelope.decoded;}
      else {ok=length>=52&&length<=maximum;logical=size_t(length);}
      if(!ok)error=ENOEXEC;
    }
    if(ok){
      bytes=static_cast<uint8_t*>(hooks.allocate(logical));
      if(!safe()){source.retainCustody();ok=false;}
      else if(!bytes){error=ENOMEM;ok=false;}
    }
    if(ok&&!encoded){
      memcpy(bytes,header,sizeof(header));
      while(ok&&offset<size_t(length)){
        size_t remaining=size_t(length)-offset;
        uint32_t wanted=uint32_t(remaining>4096?4096:remaining);
        if(wanted>RISC_STORAGE_FILES_IO_MAX)wanted=RISC_STORAGE_FILES_IO_MAX;
        uint32_t actual=0;ok=read(bytes+offset,wanted,actual)&&actual;
        if(ok)offset+=actual;
      }
    } else if(ok) {
      // Lambda adapters keep the original source/owner fences and physical
      // digest observation. CRC is accumulated only over newly produced bytes.
      struct State {
        decltype(read)* reader;
        decltype(safe)* safety;
        decltype(checkpoint)* check;
        uint8_t* output;
        size_t produced=0,physical=Envelope::HeaderBytes;
        uint32_t crc=UINT32_MAX;
      } state{&read,&safe,&checkpoint,bytes};
      const Decoder::Hooks decoder{
        &state,
        [](void* raw,uint8_t* out,size_t wanted,size_t* actual)->Decoder::Status{
          auto& s=*static_cast<State*>(raw);uint32_t got=0;
          const bool ok=(*s.reader)(out,uint32_t(wanted),got);*actual=got;
          if(!(*s.safety)())return Decoder::Retained;
          if(!ok)return Decoder::Io;
          s.physical+=got;return Decoder::Ok;
        },
        [](void* raw)->bool{return (*static_cast<State*>(raw)->safety)();},
        [](void* raw,size_t produced)->Decoder::Status{
          auto& s=*static_cast<State*>(raw);
          s.crc=Envelope::crcAdd(s.crc,s.output+s.produced,produced-s.produced);
          const bool ok=(*s.check)(produced-s.produced);s.produced=produced;
          return !(*s.safety)()?Decoder::Retained:ok?Decoder::Ok:Decoder::Io;
        }};
      size_t written=0;
      const auto rc=Decoder::decode(decoder,envelope.payload,bytes,logical,logical,&written);
      if(rc==Decoder::Retained)source.retainCustody();
      ok=rc==Decoder::Ok&&written==logical&&(state.crc^UINT32_MAX)==envelope.crc;
      if(!ok&&error!=ETIMEDOUT)error=ENOEXEC;
      offset=state.physical;
    }
    // Physical EOF and position refer to the SAME opened envelope/raw object.
    if(ok){
      uint8_t extra;uint32_t actual=0;ok=read(&extra,1,actual)&&!actual;
      if(ok){ok=checkpoint(0)&&source.info(handle,&length,&position)==RISC_FILES_OK;if(!safe()){source.retainCustody();ok=false;}}
      ok=ok&&length==offset&&position==offset;
    }
    if(handle){
      if(!safe()){source.retainCustody();retained=true;ok=false;}
      else if(source.close(handle)!=RISC_FILES_OK){retained=true;ok=false;}
      else {handle=0;if(!safe()){source.retainCustody();retained=true;ok=false;}}
    }
    retained=retained||source.retained();
    ok=ok&&!retained&&checkpoint(0);
    retained=retained||source.retained();ok=ok&&!retained;
    if(ok){size=logical;physicalSize=offset;error=0;return true;}
    if(retained)error=EIO;
    else {if(bytes)hooks.free(bytes);bytes=nullptr;}
    size=physicalSize=0;return false;
  }

};
}
