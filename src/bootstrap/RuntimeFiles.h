#pragma once
#include "StorageFilesVolumeAdapter.h"
namespace RiscBoot {
// Trusted internal client of the same generic file ABI used by applications.
// Binding is explicit and immutable. There is no filesystem fallback. The
// platform owns the provider table until a real restart after lost custody.
class RuntimeFiles final {
 public:
  using Check=bool (*)();
  bool bind(const risc_storage_files_v1* table,Check owner,Check safe,
            const risc_storage_files_identity_v1* identity=nullptr,
            const risc_storage_files_directory_v1* directory=nullptr) {
    if(identity && (!table || identity->api_version!=1 || identity->struct_size<sizeof(*identity) ||
       identity->context!=table->context || !identity->path || !identity->opened))return false;
    if(directory && (!table || directory->api_version!=1 || directory->struct_size<sizeof(*directory) || directory->context!=table->context || !directory->next))return false;
    if(retired_ || !owner || !safe || !owner() || !safe() || calling_ || !files_.bindFiles(table))return false;
    owner_=owner;safe_=safe;identity_=identity;directory_=directory;return true;
  }
  // Native assembly bridge only. Its graph grant outlives this client, and no
  // invocation-bound table or backend selection leaks into generic callers.
  bool bindVolume(const risc_storage_volume_api_v1* table,Check owner,Check safe) {
    if(retired_ || !owner || !safe || !owner() || !safe() || calling_ || !files_.bind(table))return false;
    owner_=owner;safe_=safe;return true;
  }
  // Successful permanent-provider shutdown invalidates the borrowed table.
  // Retirement is one-way; this object can never be rebound to another store.
  bool retire(){if(sleeping_ || !exitSafe() || !owner_ || !owner_())return false;retired_=true;return true;}
  // Internal transaction fence. No provider or handle ownership is released.
  bool suspendSleep(){if(sleeping_ || !exitSafe() || !owner_ || !owner_() || !safe_())return false;sleeping_=true;return true;}
  bool resumeSleep(){if(!sleeping_ || !exitSafe() || !owner_ || !owner_() || !safe_())return false;sleeping_=false;return true;}
  int32_t identity(const char*p,risc_storage_files_identity_value_v1*v){return call([&]{return files_.identity(identity_,p,0,v);});}
  int32_t identity(uint32_t h,risc_storage_files_identity_value_v1*v){return call([&]{return files_.identity(identity_,nullptr,h,v);});}
  // Internal consumers must preserve a known open token if cleanup cannot
  // dispatch (for example owner/safety changed before close).
  void retainCustody(){retained_=true;}
  int32_t directoryNext(uint32_t h,risc_storage_files_stat_v1*s,char*n,uint32_t cap,uint32_t*required){if(required)*required=0;return call([&]{return files_.directoryNext(directory_,h,s,n,cap,required);});}
  bool bound()const{return !retired_ && files_.bound();}
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
  // Caller-owned output; initialized for every outcome and never stored.
  int32_t stat(const char*p,risc_storage_files_stat_v1*s,StatObservation* observation=nullptr){if(observation)*observation=emptyStatObservation();return call([&]{return files_.stat(p,s,observation);},observation);}
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
  template<class F>int32_t call(F f,StatObservation* observation=nullptr){
    if(retained()){if(observation)observation->flags|=StatRetained;return RISC_FILES_RETAINED;}
    if(!bound())return RISC_FILES_UNAVAILABLE;
    if(observation)observation->flags|=StatBound;
    if(calling_ || sleeping_)return RISC_FILES_BUSY;
    if(!owner_())return RISC_FILES_CONTEXT;
    if(!safe_())return RISC_FILES_BUSY;
    calling_=true;const int32_t rc=f();calling_=false;
    // Losing the provider after dispatch makes cleanup unsafe. Do not issue a
    // speculative close, retry, or switch to another storage implementation.
    if(rc==RISC_FILES_RETAINED || !owner_() || !safe_()){retained_=true;return RISC_FILES_RETAINED;}
    return rc;
  }
  const risc_storage_files_identity_v1* identity_=nullptr;
  const risc_storage_files_directory_v1* directory_=nullptr;
  StorageFilesVolumeAdapter files_;Check owner_=nullptr,safe_=nullptr;
  bool calling_=false,retained_=false,retired_=false,sleeping_=false;
};
// One explicitly selected platform-owned internal store service. Multiple
// Runtime candidates use the same authority while comparing active/staged
// roots. No app can bind, replace, or acquire this privileged client.
inline RuntimeFiles& runtimeFiles(){static RuntimeFiles files;return files;}
}

namespace RiscBoot {
// Trusted immutable source map. Names select generic namespace sessions, not
// physical backends. Every candidate shares its map's terminal custody barrier.
class RuntimeFileSources final {
 public:
  struct Entry {const char* root;RuntimeFiles* files;};
  constexpr RuntimeFileSources(const Entry* entries,size_t count):entries_(entries),count_(count){}
  RuntimeFiles* find(const char* root)const {
    if(!RiscStorage::validPath(root) || !entries_)return nullptr;
    RuntimeFiles* result=nullptr;
    for(size_t i=0;i<count_;++i){
      const auto&e=entries_[i];if(!RiscStorage::validPath(e.root)||!e.files||!e.files->bound())return nullptr;
      for(size_t j=0;j<i;++j)if(!strcmp(e.root,entries_[j].root))return nullptr;
      if(!strcmp(e.root,root))result=e.files;
    }
    return result;
  }
  bool retained()const {if(entries_)for(size_t i=0;i<count_;++i)if(entries_[i].files&&entries_[i].files->retained())return true;return false;}
  bool exitSafe()const {if(entries_)for(size_t i=0;i<count_;++i)if(entries_[i].files&&!entries_[i].files->exitSafe())return false;return true;}
 private:
  const Entry* entries_;size_t count_;
};
// A read-only view of one explicitly named source. The whole source map is a
// terminal custody fence, including failures originating in another candidate.
// Names below remain logical root-qualified paths; only the selected session
// sees the namespace-relative suffix. No provider lookup or fallback occurs
// after construction, and the map/root/provider lifetimes are borrowed.
class RuntimeFileView final {
 public:
  RuntimeFileView(const RuntimeFileSources* sources,const char* root):sources_(sources),root_(root),
    files_(sources?sources->find(root):nullptr){}
  bool retained()const{return sources_ && sources_->retained();}
  bool bound()const{return files_!=nullptr;}
  void retainCustody(){if(files_)files_->retainCustody();}
  int32_t open(const char*p,uint32_t*h){if(h)*h=0;return call([&]{const char*n=name(p);return n?files_->open(n,RISC_FILES_READ,h):int32_t(RISC_FILES_INVALID);});}
  int32_t close(uint32_t h){
    const int32_t rc=call([&]{return files_->close(h);});
    if(rc!=RISC_FILES_OK && files_){files_->retainCustody();return RISC_FILES_RETAINED;}
    return rc;
  }
  int32_t read(uint32_t h,void*b,uint32_t n,uint32_t*a){if(a)*a=0;return call([&]{return files_->read(h,b,n,a);});}
  // Bounded caller-owned metadata buffer. Legal short reads are continued;
  // one extra byte proves complete EOF without allocating another buffer.
  bool readToEnd(uint32_t h,void*data,uint32_t capacity,uint32_t*actual){
    if(!actual || (!data&&capacity))return false;
    *actual=0;auto*bytes=static_cast<unsigned char*>(data);
    while(*actual<capacity){uint32_t got=0,wanted=capacity-*actual;
      if(wanted>RISC_STORAGE_FILES_IO_MAX)wanted=RISC_STORAGE_FILES_IO_MAX;
      const int32_t rc=read(h,bytes+*actual,wanted,&got);*actual+=got;
      if(rc!=RISC_FILES_OK)return false;
      if(!got)return true;
    }
    unsigned char extra=0;uint32_t got=0;return read(h,&extra,1,&got)==RISC_FILES_OK && !got;
  }
  int32_t seek(uint32_t h,uint64_t o){return call([&]{return files_->seek(h,o);});}
  int32_t info(uint32_t h,uint64_t*n,uint64_t*o){return call([&]{return files_->info(h,n,o);});}
  int32_t identity(const char*p,risc_storage_files_identity_value_v1*v){return call([&]{const char*n=name(p);return n?files_->identity(n,v):int32_t(RISC_FILES_INVALID);});}
  int32_t identity(uint32_t h,risc_storage_files_identity_value_v1*v){return call([&]{return files_->identity(h,v);});}
  int32_t stat(const char*p,risc_storage_files_stat_v1*s){return call([&]{const char*n=name(p);return n?files_->stat(n,s):int32_t(RISC_FILES_INVALID);});}
  int32_t dirOpen(const char*p,uint32_t*h){if(h)*h=0;return call([&]{const char*n=name(p);return n?files_->dirOpen(n,h):int32_t(RISC_FILES_INVALID);});}
  int32_t dirNext(uint32_t h,risc_storage_files_entry_v1*e){return call([&]{return files_->dirNext(h,e);});}
  int32_t directoryNext(uint32_t h,risc_storage_files_stat_v1*s,char*n,uint32_t cap,uint32_t*required){if(required)*required=0;return call([&]{return files_->directoryNext(h,s,n,cap,required);});}
  int32_t dirClose(uint32_t h){
    const int32_t rc=call([&]{return files_->dirClose(h);});
    if(rc!=RISC_FILES_OK && files_){files_->retainCustody();return RISC_FILES_RETAINED;}
    return rc;
  }
 private:
  const char* name(const char*p)const {
    if(!RiscStorage::validPath(p))return nullptr;
    if(!strcmp(root_,"/"))return p;
    const size_t n=strlen(root_);
    if(strncmp(p,root_,n))return nullptr;
    return !p[n]?"/":p[n]=='/'?p+n:nullptr;
  }
  template<class F>int32_t call(F f){
    if(retained())return RISC_FILES_RETAINED;
    if(!files_)return RISC_FILES_UNAVAILABLE;
    const int32_t rc=f();return retained()?int32_t(RISC_FILES_RETAINED):rc;
  }
  const RuntimeFileSources* sources_;const char* root_;RuntimeFiles* files_;
};
inline bool sameFileIdentity(const risc_storage_files_identity_value_v1&a,const risc_storage_files_identity_value_v1&b){
 return a.struct_size==sizeof(a)&&b.struct_size==sizeof(b)&&!a.reserved&&!b.reserved&&
   a.size==b.size&&!memcmp(a.opaque,b.opaque,sizeof(a.opaque));
}
}
