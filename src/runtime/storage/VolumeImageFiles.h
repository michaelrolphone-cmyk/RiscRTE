#pragma once
#include <RiscStorageFilesV1.h>
#include <RiscStorageVolumeV1.h>
#include "FileValidation.h"
#include <cstring>
namespace RiscStorage {
// Concrete read-only compatibility provider for existing volume image sources.
// All media refresh/readiness and legacy handle semantics stop at this boundary.
// This does not add CRUD authority or require existing products to migrate data.
class VolumeImageFiles final {
 public:
  explicit VolumeImageFiles(const risc_storage_volume_api_v1* volume,bool(*safe)(void*),void* context):volume_(volume),safe_(safe),context_(context) {
    api_={1,sizeof(api_),this,
      [](void*c,const char*p,uint32_t f,uint32_t*h){return self(c).open(p,f,h);},
      [](void*c,uint32_t h){return self(c).close(h);},
      [](void*c,uint32_t h,void*b,uint32_t n,uint32_t*a){return self(c).read(h,b,n,a);},
      [](void*,uint32_t,const void*,uint32_t,uint32_t*a)->int32_t{if(a)*a=0;return RISC_FILES_DENIED;},
      [](void*,uint32_t,uint64_t)->int32_t{return RISC_FILES_DENIED;},
      [](void*c,uint32_t h,uint64_t*n,uint64_t*o){return self(c).info(h,n,o);},
      [](void*,uint32_t)->int32_t{return RISC_FILES_DENIED;},
      [](void*,uint32_t,uint64_t)->int32_t{return RISC_FILES_DENIED;},
      [](void*,const char*,risc_storage_files_stat_v1*)->int32_t{return RISC_FILES_DENIED;},
      [](void*,const char*)->int32_t{return RISC_FILES_DENIED;},
      [](void*,const char*,const char*)->int32_t{return RISC_FILES_DENIED;},
      [](void*,const char*)->int32_t{return RISC_FILES_DENIED;},
      [](void*,const char*,uint32_t*h)->int32_t{if(h)*h=0;return RISC_FILES_DENIED;},
      [](void*,uint32_t,risc_storage_files_entry_v1*)->int32_t{return RISC_FILES_DENIED;},
      [](void*,uint32_t)->int32_t{return RISC_FILES_DENIED;},
      [](void*,uint32_t)->int32_t{return RISC_FILES_DENIED;},
      [](void*,uint32_t,uint64_t*)->int32_t{return RISC_FILES_DENIED;},
      [](void*,uint32_t,uint64_t)->int32_t{return RISC_FILES_DENIED;}};
  }
  const risc_storage_files_v1* api()const{return &api_;}
 private:
  static VolumeImageFiles& self(void*c){return *static_cast<VolumeImageFiles*>(c);}
  int32_t retain(){retained_=true;return RISC_FILES_RETAINED;}
  bool safe()const{return safe_ && safe_(context_);}
  int32_t open(const char*p,uint32_t f,uint32_t*out) {
    if(out)*out=0;
    if(retained_)return RISC_FILES_RETAINED;
    if(!out || f!=RISC_FILES_READ || !validPath(p))return RISC_FILES_INVALID;
    if(handle_)return RISC_FILES_BUSY;
    const auto*v=volume_;
    if(!v || v->api_version!=1 || v->struct_size<sizeof(*v) || !v->refresh || !v->ready ||
       !v->file_open_read || !v->file_read || !v->file_close)return RISC_FILES_UNAVAILABLE;
    if(!safe())return RISC_FILES_BUSY;
    bool ready=v->ready(v->context);if(!safe())return retain();
    if(!ready){const bool refreshed=v->refresh(v->context);if(!safe())return retain();
      if(!refreshed)return RISC_FILES_UNAVAILABLE;
      ready=v->ready(v->context);if(!safe())return retain();
      if(!ready)return RISC_FILES_UNAVAILABLE;
    }
    handle_=v->file_open_read(v->context,p,&size_);offset_=0;
    if(!safe())return retain();
    // Base volume open cannot distinguish missing from busy; do not invent an
    // authoritative NOT_FOUND result from its boolean failure.
    if(!handle_)return RISC_FILES_UNAVAILABLE;
    *out=1;return RISC_FILES_OK;
  }
  int32_t close(uint32_t h) {
    if(retained_)return RISC_FILES_RETAINED;
    if(h!=1 || !handle_)return RISC_FILES_INVALID;
    if(!safe())return retain();
    const bool closed=volume_->file_close(volume_->context,handle_,false);
    if(!closed || !safe())return retain();
    handle_=0;return RISC_FILES_OK;
  }
  int32_t read(uint32_t h,void*b,uint32_t n,uint32_t*a) {
    if(a)*a=0;
    if(retained_)return RISC_FILES_RETAINED;
    if(h!=1 || !handle_ || !a || (!b&&n) || n>RISC_STORAGE_FILES_IO_MAX)return RISC_FILES_INVALID;
    if(!safe())return retain();
    const size_t got=volume_->file_read(volume_->context,handle_,b,n);
    if(!safe())return retain();
    if(got>n)return retain();
    *a=uint32_t(got);offset_+=got;
    const auto*ext=risc_storage_volume_extension(volume_);
    if(ext && ext->handle_error){const auto error=ext->handle_error(volume_->context,handle_,false);
      if(!safe())return retain();
      if(error)return RISC_FILES_IO;
    }
    return RISC_FILES_OK;
  }
  int32_t info(uint32_t h,uint64_t*n,uint64_t*o) {
    if(retained_)return RISC_FILES_RETAINED;
    if(h!=1 || !handle_ || !n || !o)return RISC_FILES_INVALID;
    *n=size_;*o=offset_;return RISC_FILES_OK;
  }
  const risc_storage_volume_api_v1* volume_;bool(*safe_)(void*);void* context_;risc_storage_files_v1 api_{};
  uint32_t handle_=0;uint64_t size_=0,offset_=0;bool retained_=false;
};
}
