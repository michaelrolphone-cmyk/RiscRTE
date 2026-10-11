#pragma once
#include <RiscStorageVolumeV1.h>
#include <cstddef>
#include <cstdint>

namespace RiscStorage {
/* Capability adapter, not a mount or filesystem. The admitted upstream must
 * implement the canonical checked-close/exclusive-rename extension and forbid
 * symlink/reparse/hard-link aliases outside its path tree. All instances use one
 * serialized executor. Hooks validate the owner and
 * fence revoked/retained provider custody. Keep this object and its upstream
 * mapped until end() succeeds. No destructor performs uncertain cleanup.
 * configure() does no I/O; begin() checks the explicit, already-existing root.
 * begin() exposes the unchanged V1 prefix. beginExtended() explicitly opts in
 * to checked directory close, local handle errors and confined mkdir/rename;
 * unsupported extension callbacks remain null. */
class ScopedUserVolume final {
 public:
  struct Hooks { void* context; bool (*owner)(void*); bool (*safe)(void*); };
  static constexpr size_t PathMax=192, IoMax=512, SkipMax=8, SlotMax=4;
  explicit ScopedUserVolume(Hooks hooks):hooks_(hooks){}
  ~ScopedUserVolume();
  ScopedUserVolume(const ScopedUserVolume&)=delete;
  ScopedUserVolume& operator=(const ScopedUserVolume&)=delete;
  bool configure(const risc_storage_volume_api_v1*,const char* root,const char* label);
  bool begin(risc_storage_volume_api_v1* out);
  bool beginExtended(risc_storage_volume_api_v1_ext* out);
  static bool validRoot(const char* root) { return valid(root,true); }
  bool end();
  bool retained() const { return retained_; }
  bool exitSafe() const { return !retained_ && !busy_ && !active_; }
 private:
  static ScopedUserVolume* slots_[SlotMax];
  static uintptr_t nextContext_;
  static uint32_t nextStage_,nextHandle_;
  Hooks hooks_;
  const risc_storage_volume_api_v1_ext* upstream_=nullptr;
  char root_[PathMax+1]{},label_[64]{},error_[128]{};
  bool configured_=false,active_=false,busy_=false,retained_=false,extendedSupported_=false;
  bool writing_=false,fileFailed_=false,dirFailed_=false;
  bool readFailed_=false;
  uintptr_t context_=0;
  uint32_t file_=0,dir_=0,upstreamFile_=0,upstreamDir_=0;
  uint32_t read_=0,upstreamRead_=0;
  uint64_t size_=0,offset_=0;
  uint64_t readSize_=0,readOffset_=0;
  char destination_[RISC_STORAGE_VOLUME_PATH_MAX]{},stage_[RISC_STORAGE_VOLUME_PATH_MAX]{};
  static ScopedUserVolume* resolve(void*);
  static bool valid(const char*,bool root=false);
  static bool reserved(const char*);
  static bool copy(char*,size_t,const char*);
  bool path(const char*,char*,bool allowRoot=true);
  bool fail(const char*);
  bool retain(const char*);
  bool enter();
  bool leave();
  bool providerError(const char*);
  template<class F> bool call(F f) {
    if(!enter())return false;
    const bool result=f();
    return leave() && result;
  }
  bool closeFile(bool);
  bool closeRead();
  bool closeDir();
  bool ready();
  bool refresh();
  bool stat(const char*,uint64_t*,bool*);
  uint32_t dirOpen(const char*);
  bool dirNext(uint32_t,risc_storage_dirent_v1*);
  void dirClose(uint32_t);
  bool dirCloseChecked(uint32_t);
  uint32_t handleError(uint32_t,bool);
  uint32_t fileOpenRead(const char*,uint64_t*);
  uint32_t fileOpenWrite(const char*);
  size_t fileRead(uint32_t,void*,size_t);
  size_t fileWrite(uint32_t,const void*,size_t);
  bool fileClose(uint32_t,bool);
  bool remove(const char*);
  bool mkdir(const char*);
  bool rename(const char*,const char*);
};
}
