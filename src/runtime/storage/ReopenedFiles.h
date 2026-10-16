#pragma once
#include <RiscStorageFilesV1.h>
#include "FileValidation.h"
#include <cstddef>
#include <cstdint>

namespace RiscStorage {
// Optional provider for one exclusively assigned private filesystem root.
// Logical handles contain only a path and offset between calls. Every physical
// descriptor is checked closed before releasing the mount lease. No application
// may reach this root through a second provider. No mount/format/fallback occurs.
class ReopenedFiles final {
 public:
  static constexpr uint32_t DirectoryLimit=128;
  static constexpr uint64_t ReservedBytes=139264; // Existing private-state headroom.
  struct Hooks {
    void* context;
    bool (*enter)(void*);
    bool (*leave)(void*);
    bool (*retained)(void*);
    void (*retain)(void*);
    bool (*usage)(void*,uint64_t*,uint64_t*);
  };
  explicit ReopenedFiles(Hooks hooks);
  bool configure(const char* root); // Metadata only, once, immutable.
  int32_t provision(); // Explicit owner admission creates this private root only.
  const risc_storage_files_v1* api()const{return &api_;}
  bool retained()const{return retained_ || (hooks_.retained&&hooks_.retained(hooks_.context));}
  bool exitSafe()const{return !busy_&&!token_&&!retained();}
 private:
  template<class F> int32_t call(F fn){
    if(retained())return RISC_FILES_RETAINED;
    if(!configured_)return RISC_FILES_UNAVAILABLE;
    if(busy_)return RISC_FILES_BUSY;
    if(!hooks_.enter(hooks_.context))return retained()?RISC_FILES_RETAINED:RISC_FILES_BUSY;
    busy_=true;
    int32_t result=fn();
    if(retained() || descriptor_>=0 || directory_)result=retain();
    else if(!hooks_.leave(hooks_.context))result=retain();
    busy_=false;
    return result;
  }
  int32_t retain();
  int32_t error();
  int32_t closePhysical(int32_t result,bool writing=false);
  int32_t closeDirectory(int32_t result);
  bool path(const char*,char*,size_t)const;
  int32_t space(uint64_t growth);
  int32_t inspect(const char*,risc_storage_files_stat_v1*);
  int32_t reopen(bool write);
  int32_t open(const char*,uint32_t,uint32_t*);
  int32_t close(uint32_t);
  int32_t read(uint32_t,void*,uint32_t,uint32_t*);
  int32_t write(uint32_t,const void*,uint32_t,uint32_t*);
  int32_t seek(uint32_t,uint64_t);
  int32_t info(uint32_t,uint64_t*,uint64_t*);
  int32_t sync(uint32_t);
  int32_t truncate(uint32_t,uint64_t);
  int32_t stat(const char*,risc_storage_files_stat_v1*);
  int32_t mkdir(const char*);
  int32_t remove(const char*);
  int32_t dirOpen(const char*,uint32_t*);
  int32_t dirNext(uint32_t,risc_storage_files_entry_v1*);
  int32_t dirClose(uint32_t);
  int32_t dirSeek(uint32_t,uint64_t);
  int32_t dirTell(uint32_t,uint64_t*);
  bool file(uint32_t token)const{return token&&token==token_&&!isDirectory_;}
  bool dir(uint32_t token)const{return token&&token==token_&&isDirectory_;}
  Hooks hooks_;risc_storage_files_v1 api_{};
  char root_[96]{},selected_[RISC_STORAGE_FILES_PATH_MAX]{};
  uint64_t offset_=0,size_=0;
  uint32_t token_=0,generation_=0,flags_=0;
  int descriptor_=-1;void* directory_=nullptr;
  bool configured_=false,busy_=false,retained_=false,isDirectory_=false;
};
}
