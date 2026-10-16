#pragma once
#include <RiscStorageFilesIdentityV1.h>
#include <RiscStorageFilesDirectoryV1.h>
/* Native adapter only. Backend custody and all legacy volume details stop here;
 * the app table is RiscStorageFilesV1.h. Runtime owns invocation authorization,
 * graph grants and propagation of sticky retention. Never expose this class to
 * an application. A direct storage.files provider needs no legacy interfaces.
 * For the legacy bridge only, metadata suffix is mandatory: boolean stat cannot
 * distinguish a missing file from an I/O failure. */
#include <RiscStorageFilesV1.h>
#include "runtime/storage/FileValidation.h"
#include <RiscStorageVolumeFsV1.h>
#include <cstring>
#include <climits>
namespace RiscBoot {
static_assert(int(RISC_FILES_READ)==int(RISC_STORAGE_OPEN_READ) &&
 int(RISC_FILES_WRITE)==int(RISC_STORAGE_OPEN_WRITE) && int(RISC_FILES_CREATE)==int(RISC_STORAGE_OPEN_CREATE) &&
 int(RISC_FILES_TRUNCATE)==int(RISC_STORAGE_OPEN_TRUNCATE) && int(RISC_FILES_EXCLUSIVE)==int(RISC_STORAGE_OPEN_EXCLUSIVE) &&
 int(RISC_FILES_APPEND)==int(RISC_STORAGE_OPEN_APPEND), "file open flag translation changed");
static_assert(int(RISC_FILES_DIRECTORY)==int(RISC_STORAGE_DIRECTORY) &&
 int(RISC_FILES_MODIFIED_VALID)==int(RISC_STORAGE_MODIFIED_VALID) && int(RISC_FILES_MODIFIED_LOCAL)==int(RISC_STORAGE_MODIFIED_LOCAL),
 "file metadata flag translation changed");
class StorageFilesVolumeAdapter {
 public:
  static constexpr unsigned HandleCapacity=8;
  bool bind(const risc_storage_volume_api_v1* api) {
    if(bound() || retained_ || !exitSafe())return false;
    const auto* fs=risc_storage_volume_fs(api);
    const auto* v=risc_storage_volume_extension(api);
    if(!fs || !v || !api->ready || !api->dir_open || !api->dir_next ||
       !api->file_read || !api->file_write || !api->file_close || !api->remove ||
       !v->file_open || !v->file_seek || !v->file_info || !v->file_sync ||
       !v->dir_rewind || !v->dir_close_checked || !v->handle_error || !v->mkdir || !v->rename)return false;
    volume_=v;fs_=fs;return true;
  }
  bool bindFiles(const risc_storage_files_v1* api) {
    if(bound() || retained_ || !exitSafe() || !api || api->api_version!=RISC_STORAGE_FILES_API_V1 ||
       api->struct_size<sizeof(*api) || !api->open || !api->close || !api->read || !api->write ||
       !api->seek || !api->info || !api->sync || !api->truncate || !api->stat || !api->mkdir ||
       !api->rename || !api->remove || !api->dir_open || !api->dir_next || !api->dir_close ||
       !api->dir_rewind || !api->dir_tell || !api->dir_seek)return false;
    files_=api;return true;
  }
  bool bound() const {return volume_!=nullptr || files_!=nullptr;}
  bool retained() const {return retained_;}
  bool exitSafe() const {if(retained_)return false;for(const auto& h:handles_)if(h.native)return false;return true;}
  int32_t open(const char* path,uint32_t flags,risc_storage_files_handle_v1* out) {
    if(out)*out=0;
    if(!out || !RiscStorage::validPath(path) || !RiscStorage::validFlags(flags))return RISC_FILES_INVALID;
    int32_t rc=before();if(rc!=RISC_FILES_OK)return rc;
    Handle* h=vacant();if(!h || generation_==UINT32_MAX)return RISC_FILES_BUSY;
    if(files_) {
      uint32_t native=0;rc=files_->open(files_->context,path,flags,&native);
      return opened(h,native,flags,false,rc,out);
    }
    // Establish missing/existing status without interpreting legacy error text.
    risc_storage_files_stat_v1 st{};st.struct_size=sizeof(st);rc=stat(path,&st);
    if(rc==RISC_FILES_OK) {
      if(st.flags&RISC_FILES_DIRECTORY)return RISC_FILES_INVALID;
      if((flags&(RISC_FILES_CREATE|RISC_FILES_EXCLUSIVE))==(RISC_FILES_CREATE|RISC_FILES_EXCLUSIVE))return RISC_FILES_EXISTS;
    }else if(rc!=RISC_FILES_NOT_FOUND || !(flags&RISC_FILES_CREATE))return rc;
    uint32_t native=volume_->file_open(context(),path,flags);
    if(native){h->native=native;h->token=++generation_;h->flags=flags;h->directory=false;}
    rc=after(native!=0,true);
    if(rc==RISC_FILES_OK)*out=h->token;
    return rc;
  }
  int32_t close(risc_storage_files_handle_v1 token) {return closeHandle(token,false);}
  int32_t read(risc_storage_files_handle_v1 token,void* bytes,uint32_t cap,uint32_t* count) {
    if(count)*count=0;
    if(!count || (!bytes&&cap) || cap>RISC_STORAGE_FILES_IO_MAX)return RISC_FILES_INVALID;
    Handle* h=find(token,false);if(!h)return RISC_FILES_INVALID;
    if(!(h->flags&RISC_FILES_READ))return RISC_FILES_DENIED;
    int32_t rc=before();if(rc!=RISC_FILES_OK)return rc;
    if(files_) {
      uint32_t n=0;rc=directResult(files_->read(files_->context,h->native,bytes,cap,&n));
      if(n>cap)return retain();
      *count=n;return rc;
    }
    size_t n=volume_->base.file_read(context(),h->native,bytes,cap);
    rc=after(true,false);if(rc!=RISC_FILES_OK)return rc;
    if(n>cap)return retain();
    *count=uint32_t(n);
    return handleStatus(*h);
  }
  int32_t write(risc_storage_files_handle_v1 token,const void* bytes,uint32_t size,uint32_t* count) {
    if(count)*count=0;
    if(!count || (!bytes&&size) || size>RISC_STORAGE_FILES_IO_MAX)return RISC_FILES_INVALID;
    Handle* h=find(token,false);if(!h)return RISC_FILES_INVALID;
    if(!(h->flags&RISC_FILES_WRITE))return RISC_FILES_DENIED;
    int32_t rc=before();if(rc!=RISC_FILES_OK)return rc;
    if(files_) {
      uint32_t n=0;rc=directResult(files_->write(files_->context,h->native,bytes,size,&n));
      if(n>size)return retain();
      *count=n;return rc;
    }
    size_t n=volume_->base.file_write(context(),h->native,bytes,size);
    rc=after(true,false);if(rc!=RISC_FILES_OK)return rc;
    if(n>size)return retain();
    *count=uint32_t(n);
    return handleStatus(*h);
  }
  // Internal optional supplement: map grant-owned tokens before calling the
  // matching provider. Raw handles never escape through the application ABI.
  int32_t identity(const risc_storage_files_identity_v1* api,const char* path,
                   uint32_t token,risc_storage_files_identity_value_v1* out) {
    if(!out || out->struct_size<sizeof(*out))return RISC_FILES_INVALID;
    if(!files_ || !api || api->api_version!=1 || api->struct_size<sizeof(*api) ||
       api->context!=files_->context || !api->path || !api->opened)return RISC_FILES_UNAVAILABLE;
    if(path && !RiscStorage::validPath(path))return RISC_FILES_INVALID;
    Handle* h=path?nullptr:find(token,false);if(!path&&!h)return RISC_FILES_INVALID;
    int32_t rc=before();if(rc!=RISC_FILES_OK)return rc;
    risc_storage_files_identity_value_v1 value{};value.struct_size=sizeof(value);
    rc=directResult(path?api->path(api->context,path,&value):api->opened(api->context,h->native,&value));
    if(rc!=RISC_FILES_OK)return rc;
    if(value.struct_size!=sizeof(value)||value.reserved)return RISC_FILES_IO;
    *out=value;return RISC_FILES_OK;
  }
  int32_t directoryNext(const risc_storage_files_directory_v1* api,uint32_t token,
                        risc_storage_files_stat_v1*out,char*name,uint32_t capacity,uint32_t*required){
    if(required)*required=0;
    if(!out||out->struct_size<sizeof(*out)||!required||(!name&&capacity))return RISC_FILES_INVALID;
    Handle*h=find(token,true);if(!h)return RISC_FILES_INVALID;
    int32_t rc=before();if(rc)return rc;
    if(!api){
      if(capacity<RISC_STORAGE_FILES_NAME_MAX)return RISC_FILES_UNAVAILABLE;
      risc_storage_files_entry_v1 entry{};entry.struct_size=sizeof(entry);rc=dirNext(token,&entry);if(rc)return rc;
      const size_t bytes=strlen(entry.name)+1;memcpy(name,entry.name,bytes);*required=uint32_t(bytes);
      *out={sizeof(*out),entry.flags,entry.size,0};return RISC_FILES_OK;
    }
    if(!files_||api->api_version!=1||api->struct_size<sizeof(*api)||api->context!=files_->context||!api->next)return RISC_FILES_UNAVAILABLE;
    risc_storage_files_stat_v1 value{};value.struct_size=sizeof(value);uint32_t needed=0;
    rc=api->next(api->context,h->native,&value,name,capacity,&needed);
    if(rc==RISC_FILES_DIRECTORY_CAPACITY){if(needed<=capacity||needed>RISC_STORAGE_FILES_PATH_MAX)return RISC_FILES_IO;*required=needed;return rc;}
    rc=directResult(rc);if(rc)return rc;
    if(value.struct_size!=sizeof(value)||value.flags&~uint32_t(RISC_FILES_DIRECTORY)||value.modified_seconds||!needed||needed>capacity||needed>RISC_STORAGE_FILES_PATH_MAX||strnlen(name,capacity)!=needed-1)return RISC_FILES_IO;
    char checked[RISC_STORAGE_FILES_PATH_MAX+1];checked[0]='/';memcpy(checked+1,name,needed);
    if(!RiscStorage::validPath(checked))return RISC_FILES_IO;
    *out=value;*required=needed;return RISC_FILES_OK;
  }
  int32_t seek(risc_storage_files_handle_v1 token,uint64_t offset) {
    Handle* h=find(token,false);if(!h)return RISC_FILES_INVALID;
    int32_t rc=before();if(rc!=RISC_FILES_OK)return rc;
    if(files_)return directResult(files_->seek(files_->context,h->native,offset));
    return after(volume_->file_seek(context(),h->native,offset),false);
  }
  int32_t info(risc_storage_files_handle_v1 token,uint64_t* size,uint64_t* offset) {
    if(!size || !offset)return RISC_FILES_INVALID;
    Handle* h=find(token,false);if(!h)return RISC_FILES_INVALID;
    int32_t rc=before();if(rc!=RISC_FILES_OK)return rc;uint64_t n=0,p=0;
    rc=files_?directResult(files_->info(files_->context,h->native,&n,&p)):
      after(volume_->file_info(context(),h->native,&n,&p),false);
    if(rc==RISC_FILES_OK){*size=n;*offset=p;}return rc;
  }
  int32_t sync(risc_storage_files_handle_v1 token) {
    Handle* h=find(token,false);if(!h)return RISC_FILES_INVALID;
    int32_t rc=before();if(rc!=RISC_FILES_OK)return rc;
    if(files_)return directResult(files_->sync(files_->context,h->native));
    return after(volume_->file_sync(context(),h->native),false);
  }
  int32_t truncate(risc_storage_files_handle_v1 token,uint64_t size) {
    Handle* h=find(token,false);if(!h)return RISC_FILES_INVALID;
    if(!(h->flags&RISC_FILES_WRITE))return RISC_FILES_DENIED;
    int32_t rc=before();if(rc!=RISC_FILES_OK)return rc;
    if(files_)return directResult(files_->truncate(files_->context,h->native,size));
    return after(fs_->file_truncate(context(),h->native,size),false);
  }
  int32_t stat(const char* path,risc_storage_files_stat_v1* out) {
    if(!RiscStorage::validPath(path) || !out || out->struct_size<sizeof(*out))return RISC_FILES_INVALID;
    int32_t rc=before();if(rc!=RISC_FILES_OK)return rc;
    if(files_) {
      risc_storage_files_stat_v1 value{};value.struct_size=sizeof(value);
      rc=directResult(files_->stat(files_->context,path,&value));if(rc!=RISC_FILES_OK)return rc;
      if(value.struct_size<sizeof(value))return RISC_FILES_IO;
      *out=value;return RISC_FILES_OK;
    }
    risc_storage_metadata_v1 value{};value.struct_size=sizeof(value);
    int32_t native=fs_->metadata(context(),path,&value);
    rc=after(native==RISC_STORAGE_FS_OK,native!=RISC_STORAGE_FS_OK);
    if(rc==RISC_FILES_RETAINED || rc==RISC_FILES_UNAVAILABLE)return rc;
    if(native==RISC_STORAGE_FS_NOT_FOUND)return RISC_FILES_NOT_FOUND;
    if(native==RISC_STORAGE_FS_INVALID)return RISC_FILES_INVALID;
    if(native!=RISC_STORAGE_FS_OK)return RISC_FILES_IO;
    if(value.struct_size<sizeof(value))return RISC_FILES_IO;
    *out={sizeof(*out),value.flags,value.size,value.modified_seconds};return RISC_FILES_OK;
  }
  int32_t mkdir(const char* path) {
    if(!RiscStorage::validPath(path))return RISC_FILES_INVALID;
    if(files_){int32_t rc=before();return rc==RISC_FILES_OK?directResult(files_->mkdir(files_->context,path)):rc;}
    risc_storage_files_stat_v1 st{};st.struct_size=sizeof(st);int32_t rc=stat(path,&st);
    if(rc==RISC_FILES_OK)return RISC_FILES_EXISTS;
    if(rc!=RISC_FILES_NOT_FOUND)return rc;
    return after(volume_->mkdir(context(),path),true);
  }
  int32_t rename(const char* source,const char* destination) {
    if(!RiscStorage::validPath(source) || !RiscStorage::validPath(destination))return RISC_FILES_INVALID;
    if(files_){int32_t rc=before();return rc==RISC_FILES_OK?directResult(files_->rename(files_->context,source,destination)):rc;}
    risc_storage_files_stat_v1 st{};st.struct_size=sizeof(st);int32_t rc=stat(destination,&st);
    if(rc==RISC_FILES_OK)return RISC_FILES_EXISTS;
    if(rc!=RISC_FILES_NOT_FOUND)return rc;
    rc=stat(source,&st);if(rc!=RISC_FILES_OK)return rc;
    return after(volume_->rename(context(),source,destination),true);
  }
  int32_t remove(const char* path) {
    if(!RiscStorage::validPath(path) || !std::strcmp(path,"/"))return RISC_FILES_INVALID;
    if(files_){int32_t rc=before();return rc==RISC_FILES_OK?directResult(files_->remove(files_->context,path)):rc;}
    risc_storage_files_stat_v1 st{};st.struct_size=sizeof(st);int32_t rc=stat(path,&st);
    if(rc!=RISC_FILES_OK)return rc;
    return after(volume_->base.remove(context(),path),true);
  }
  int32_t dirOpen(const char* path,risc_storage_files_handle_v1* out) {
    if(out)*out=0;
    if(!out || !RiscStorage::validPath(path))return RISC_FILES_INVALID;
    int32_t rc=before();if(rc!=RISC_FILES_OK)return rc;
    Handle* h=vacant();if(!h || generation_==UINT32_MAX)return RISC_FILES_BUSY;
    if(files_) {
      uint32_t native=0;rc=files_->dir_open(files_->context,path,&native);
      return opened(h,native,0,true,rc,out);
    }
    risc_storage_files_stat_v1 st{};st.struct_size=sizeof(st);rc=stat(path,&st);
    if(rc!=RISC_FILES_OK)return rc;
    if(!(st.flags&RISC_FILES_DIRECTORY))return RISC_FILES_INVALID;
    uint32_t native=volume_->base.dir_open(context(),path);
    if(native){h->native=native;h->token=++generation_;h->flags=0;h->directory=true;}
    rc=after(native!=0,true);if(rc==RISC_FILES_OK)*out=h->token;return rc;
  }
  int32_t dirNext(risc_storage_files_handle_v1 token,risc_storage_files_entry_v1* out) {
    if(!out || out->struct_size<sizeof(*out))return RISC_FILES_INVALID;
    Handle* h=find(token,true);if(!h)return RISC_FILES_INVALID;
    int32_t rc=before();if(rc!=RISC_FILES_OK)return rc;risc_storage_dirent_v1 entry{};
    if(files_) {
      risc_storage_files_entry_v1 value{};value.struct_size=sizeof(value);
      rc=directResult(files_->dir_next(files_->context,h->native,&value));if(rc!=RISC_FILES_OK)return rc;
      if(value.struct_size<sizeof(value) || !std::memchr(value.name,0,sizeof(value.name)))return RISC_FILES_IO;
      *out=value;return RISC_FILES_OK;
    }
    bool next=volume_->base.dir_next(context(),h->native,&entry);
    rc=after(true,false);if(rc!=RISC_FILES_OK)return rc;
    rc=handleStatus(*h);if(rc!=RISC_FILES_OK)return rc;
    if(!next)return RISC_FILES_END;
    if(!std::memchr(entry.name,0,sizeof(entry.name)))return RISC_FILES_IO;
    *out={};out->struct_size=sizeof(*out);out->flags=entry.is_directory?RISC_FILES_DIRECTORY:0;
    out->size=entry.size;std::memcpy(out->name,entry.name,sizeof(out->name));return RISC_FILES_OK;
  }
  int32_t dirClose(risc_storage_files_handle_v1 token) {return closeHandle(token,true);}
  int32_t dirRewind(risc_storage_files_handle_v1 token) {
    Handle* h=find(token,true);if(!h)return RISC_FILES_INVALID;
    int32_t rc=before();if(rc!=RISC_FILES_OK)return rc;
    if(files_)return directResult(files_->dir_rewind(files_->context,h->native));
    return after(volume_->dir_rewind(context(),h->native),false);
  }
  int32_t dirTell(risc_storage_files_handle_v1 token,uint64_t* out) {
    if(!out)return RISC_FILES_INVALID;
    Handle* h=find(token,true);if(!h)return RISC_FILES_INVALID;
    int32_t rc=before();if(rc!=RISC_FILES_OK)return rc;uint64_t offset=0;
    rc=files_?directResult(files_->dir_tell(files_->context,h->native,&offset)):
      after(fs_->dir_tell(context(),h->native,&offset),false);if(rc==RISC_FILES_OK)*out=offset;return rc;
  }
  int32_t dirSeek(risc_storage_files_handle_v1 token,uint64_t offset) {
    Handle* h=find(token,true);if(!h)return RISC_FILES_INVALID;
    int32_t rc=before();if(rc!=RISC_FILES_OK)return rc;
    if(files_)return directResult(files_->dir_seek(files_->context,h->native,offset));
    return after(fs_->dir_seek(context(),h->native,offset),false);
  }
 private:
  struct Handle {uint32_t native=0,token=0,flags=0;bool directory=false;};
  const risc_storage_files_v1* files_=nullptr;
  const risc_storage_volume_api_v1_ext* volume_=nullptr;
  const risc_storage_volume_api_v1_fs* fs_=nullptr;
  Handle handles_[HandleCapacity]{};uint32_t generation_=0;bool retained_=false;
  void* context() const {return volume_->base.context;}
  int32_t retain(){retained_=true;return RISC_FILES_RETAINED;}
  Handle* vacant(){for(auto& h:handles_)if(!h.native)return &h;return nullptr;}
  Handle* find(uint32_t token,bool directory){for(auto& h:handles_)if(h.native && h.token==token && h.directory==directory)return &h;return nullptr;}
  int32_t before() {
    if(retained_)return RISC_FILES_RETAINED;
    if(files_)return RISC_FILES_OK;
    if(!volume_)return RISC_FILES_UNAVAILABLE;
    int32_t state=fs_->state.observe(context());
    if(state==RISC_STORAGE_STATE_READY)return RISC_FILES_OK;
    if(state==RISC_STORAGE_STATE_UNAVAILABLE && exitSafe())return RISC_FILES_UNAVAILABLE;
    return retain();
  }
  int32_t directResult(int32_t rc) {
    if(rc==RISC_FILES_RETAINED)return retain();
    switch(rc) {
      case RISC_FILES_OK:case RISC_FILES_NOT_FOUND:case RISC_FILES_END:case RISC_FILES_EXISTS:
      case RISC_FILES_INVALID:case RISC_FILES_IO:case RISC_FILES_UNAVAILABLE:case RISC_FILES_BUSY:
      case RISC_FILES_DENIED:case RISC_FILES_CONTEXT:case RISC_FILES_NO_SPACE:return rc;
      default:return RISC_FILES_IO;
    }
  }
  int32_t opened(Handle* h,uint32_t native,uint32_t flags,bool directory,int32_t rc,uint32_t* out) {
    if(native){h->native=native;h->token=++generation_;h->flags=flags;h->directory=directory;}
    rc=directResult(rc);
    if(rc==RISC_FILES_OK && native){*out=h->token;return rc;}
    // A provider returning a token on failure, or claiming success without a
    // token, has not established a safely recoverable ownership outcome.
    if(native || rc==RISC_FILES_OK)return retain();
    return rc;
  }
  int32_t after(bool success,bool ambiguous) {
    int32_t rc=before();if(rc==RISC_FILES_RETAINED)return rc;
    // Failed legacy opens can have allocated a hidden native handle. A failed
    // readiness check after an attempted call cannot prove clean ownership.
    if(rc!=RISC_FILES_OK)return ambiguous?retain():rc;
    if(!success && ambiguous) {
      bool ready=volume_->base.ready(context());rc=before();
      if(rc!=RISC_FILES_OK || !ready)return retain();
    }
    return success?RISC_FILES_OK:RISC_FILES_IO;
  }
  int32_t handleStatus(const Handle& h) {
    uint32_t error=volume_->handle_error(context(),h.native,h.directory);
    int32_t rc=after(true,false);return rc==RISC_FILES_OK?int32_t(error?RISC_FILES_IO:RISC_FILES_OK):rc;
  }
  int32_t closeHandle(uint32_t token,bool directory) {
    Handle* h=find(token,directory);if(!h)return retained_?RISC_FILES_RETAINED:RISC_FILES_INVALID;
    int32_t rc=before();if(rc!=RISC_FILES_OK)return rc;
    if(files_) {
      rc=directory?files_->dir_close(files_->context,h->native):files_->close(files_->context,h->native);
      if(rc!=RISC_FILES_OK)return retain();
      *h={};return RISC_FILES_OK;
    }
    bool closed=directory?volume_->dir_close_checked(context(),h->native):volume_->base.file_close(context(),h->native,true);
    if(!closed)return retain();
    rc=after(true,false);if(rc!=RISC_FILES_OK)return retain();*h={};return RISC_FILES_OK;
  }

};
}
