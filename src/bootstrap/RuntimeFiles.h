#pragma once
#include "StorageFilesVolumeAdapter.h"
namespace RiscBoot {
// Trusted internal client of the same generic file ABI used by applications.
// Binding is explicit and immutable. There is no filesystem fallback. The
// platform owns the provider table until a real restart after lost custody.
class RuntimeFiles final {
 public:
  using Check=bool (*)();
  bool bind(const risc_storage_files_v1* table,Check owner,Check safe) {
    if(!owner || !safe || !owner() || !safe() || calling_ || !files_.bindFiles(table))return false;
    owner_=owner;safe_=safe;return true;
  }
  bool bound()const{return files_.bound();}
  bool retained()const{return retained_ || files_.retained();}
  bool exitSafe()const{return !calling_ && !retained() && files_.exitSafe();}
  int32_t open(const char*p,uint32_t f,uint32_t*h){if(h)*h=0;return call([&]{return files_.open(p,f,h);});}
  int32_t close(uint32_t h){return call([&]{return files_.close(h);});}
  int32_t read(uint32_t h,void*b,uint32_t n,uint32_t*a){if(a)*a=0;return call([&]{return files_.read(h,b,n,a);});}
  int32_t write(uint32_t h,const void*b,uint32_t n,uint32_t*a){if(a)*a=0;return call([&]{return files_.write(h,b,n,a);});}
  int32_t seek(uint32_t h,uint64_t o){return call([&]{return files_.seek(h,o);});}
  int32_t info(uint32_t h,uint64_t*n,uint64_t*o){return call([&]{return files_.info(h,n,o);});}
  int32_t sync(uint32_t h){return call([&]{return files_.sync(h);});}
  int32_t truncate(uint32_t h,uint64_t n){return call([&]{return files_.truncate(h,n);});}
  int32_t stat(const char*p,risc_storage_files_stat_v1*s){return call([&]{return files_.stat(p,s);});}
  int32_t mkdir(const char*p){return call([&]{return files_.mkdir(p);});}
  int32_t rename(const char*a,const char*b){return call([&]{return files_.rename(a,b);});}
  int32_t remove(const char*p){return call([&]{return files_.remove(p);});}
  int32_t dirOpen(const char*p,uint32_t*h){if(h)*h=0;return call([&]{return files_.dirOpen(p,h);});}
  int32_t dirNext(uint32_t h,risc_storage_files_entry_v1*e){return call([&]{return files_.dirNext(h,e);});}
  int32_t dirClose(uint32_t h){return call([&]{return files_.dirClose(h);});}
  int32_t dirRewind(uint32_t h){return call([&]{return files_.dirRewind(h);});}
  int32_t dirTell(uint32_t h,uint64_t*o){return call([&]{return files_.dirTell(h,o);});}
  int32_t dirSeek(uint32_t h,uint64_t o){return call([&]{return files_.dirSeek(h,o);});}
 private:
  template<class F>int32_t call(F f){
    if(retained())return RISC_FILES_RETAINED;
    if(!bound())return RISC_FILES_UNAVAILABLE;
    if(calling_)return RISC_FILES_BUSY;
    if(!owner_())return RISC_FILES_CONTEXT;
    if(!safe_())return RISC_FILES_BUSY;
    calling_=true;const int32_t rc=f();calling_=false;
    // Losing the provider after dispatch makes cleanup unsafe. Do not issue a
    // speculative close, retry, or switch to another storage implementation.
    if(rc==RISC_FILES_RETAINED || !owner_() || !safe_()){retained_=true;return RISC_FILES_RETAINED;}
    return rc;
  }
  StorageFilesVolumeAdapter files_;Check owner_=nullptr,safe_=nullptr;
  bool calling_=false,retained_=false;
};
// One explicitly selected platform-owned internal store service. Multiple
// Runtime candidates use the same authority while comparing active/staged
// roots. No app can bind, replace, or acquire this privileged client.
inline RuntimeFiles& runtimeFiles(){static RuntimeFiles files;return files;}
}
