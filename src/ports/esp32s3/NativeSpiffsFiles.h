#pragma once
#include "runtime/storage/PosixFiles.h"
namespace RiscCpu {
// Optional adapter for the selected IDF SPIFFS VFS. IDF 6.1 does not preserve
// POSIX CREATE/TRUNCATE/APPEND semantics in its open flag conversion. Use
// exclusive creation, checked ftruncate, and per-write append positioning.
// The native assembly owns serialization for this regular-file-only namespace;
// no app or generic Runtime consumer selects this physical implementation.
// Root enumeration returns the flat store's relative object names (which can
// contain slashes). Non-root directory operations, mkdir and no-replace rename
// are unavailable; this adapter does not invent a directory hierarchy.
// The root string and provider live until successful shutdown or real restart.
class NativeSpiffsFiles final {
 public:
  explicit NativeSpiffsFiles(const char* root):files_(root,RiscStorage::PosixFiles::OpenBehavior::SerializedFlatVfs){}
  const risc_storage_files_v1* api()const{return files_.api();}
  bool exitSafe()const{return files_.exitSafe();}
  bool retained()const{return files_.retained();}
 private:
  RiscStorage::PosixFiles files_;
};
}
