#pragma once
#include "bootstrap/FileSourceBackend.h"
#include "bootstrap/RuntimeFiles.h"
#include <cstring>
#include <cstdint>
namespace RiscStorage {
// Staged/sealed scratch semantics over an explicitly provisioned generic file
// session. No physical root, VFS flag, descriptor or fallback reaches this layer.
// Session/provider lifetimes must enclose this service, including terminal loss.
class FileSourceFiles final {
public:
 struct Hooks {
  void* context=nullptr;
  bool(*owner)(void*)=nullptr;
  bool(*safe)(void*)=nullptr;
  bool(*freeBytes)(void*,uint64_t*)=nullptr;
  uint64_t headroom=0;
  uint32_t ioMax=RISC_FILE_OPEN_SOURCE_IO_MAX;
  uint64_t maximumSize=INT64_MAX;
 };
 explicit FileSourceFiles(Hooks h):hooks_(h){}
 bool configure(RiscBoot::RuntimeFiles* files,const char* name) {
  if(configured_||retained_||!files||!files->bound()||!files->exitSafe()||!name||strcmp(name,"/.risc-file-source")||!safe()||!hooks_.ioMax||hooks_.ioMax>RISC_FILE_OPEN_SOURCE_IO_MAX||hooks_.ioMax>RISC_STORAGE_FILES_IO_MAX)return false;
  files_=files;const int32_t rc=files_->remove(name);
  if(rc!=RISC_FILES_OK&&rc!=RISC_FILES_NOT_FOUND){if(files_->retained())retain();return false;}
  if(!safe()){retain();return false;}
  configured_=true;return true;
 }
 bool exitSafe()const{return !retained_&&!busy_&&(!files_||files_->exitSafe());}
 bool retained()const{return retained_||(files_&&files_->retained());}
 bool active()const{return active_;}
 int32_t begin(uint64_t size) {
  const int32_t gate=enter();if(gate)return gate;
  if(active_)return leave(RISC_FILE_SOURCE_BUSY);
  if(size>hooks_.maximumSize)return leave(RISC_FILE_SOURCE_INVALID);
  const int32_t capacity=space(size);if(capacity)return leave(capacity);
  uint32_t token=0;const int32_t rc=open(RISC_FILES_WRITE|RISC_FILES_CREATE|RISC_FILES_EXCLUSIVE,&token);if(rc)return leave(rc);
  active_=true;sealed_=false;size_=size;written_=0;
  return leave(close(token));
 }
 int32_t write(const void* bytes,uint32_t length,uint32_t* actual) {
  if(actual)*actual=0;
  const int32_t gate=enter();if(gate)return gate;
  if(!actual||!bytes||!length||length>RISC_FILE_OPEN_SOURCE_IO_MAX||!active_||sealed_||length>size_-written_)return leave(RISC_FILE_SOURCE_INVALID);
  const uint32_t want=length<hooks_.ioMax?length:hooks_.ioMax;
  const int32_t capacity=space(want);if(capacity)return leave(capacity);
  uint32_t token=0;int32_t rc=open(RISC_FILES_WRITE,&token);if(rc)return leave(rc);
  rc=inspect(token,written_);uint32_t count=0;
  if(!rc)rc=result(files_->seek(token,written_));
  if(!rc){rc=result(files_->write(token,bytes,want,&count));if(!rc&&!count)rc=RISC_FILE_SOURCE_NO_SPACE;}
  if(retained())return leave(RISC_FILE_SOURCE_RETAINED);
  const auto closed=close(token);if(closed)return leave(closed);
  rc=leave(rc);if(!rc){written_+=count;*actual=count;}return rc;
 }
 int32_t finish() {
  const int32_t gate=enter();if(gate)return gate;
  if(!active_||sealed_)return leave(RISC_FILE_SOURCE_INVALID);
  if(written_!=size_)return leave(RISC_FILE_SOURCE_INCOMPLETE);
  uint32_t token=0;int32_t rc=open(RISC_FILES_READ,&token);if(rc)return leave(rc);
  rc=inspect(token,size_);
  if(retained())return leave(RISC_FILE_SOURCE_RETAINED);
  const auto closed=close(token);if(closed)return leave(closed);
  rc=leave(rc);if(!rc)sealed_=true;return rc;
 }
 int32_t readAt(uint64_t offset,void* out,uint32_t capacity,uint32_t* actual) {
  if(actual)*actual=0;
  const int32_t gate=enter();if(gate)return gate;
  if(!actual||(!out&&capacity)||capacity>RISC_FILE_OPEN_SOURCE_IO_MAX||!active_||!sealed_||offset>size_)return leave(RISC_FILE_SOURCE_INVALID);
  uint32_t want=uint32_t(size_-offset<capacity?size_-offset:capacity);
  if(want>hooks_.ioMax)want=hooks_.ioMax;
  if(!want)return leave(RISC_FILE_SOURCE_OK);
  uint32_t token=0;int32_t rc=open(RISC_FILES_READ,&token);if(rc)return leave(rc);
  // Short reads or changed lengths contradict this sealed source. Retain the
  // generic session before close/cleanup, irrespective of backing mechanism.
  unsigned char bytes[RISC_FILE_OPEN_SOURCE_IO_MAX];rc=inspect(token,size_);uint32_t got=0;
  if(!rc)rc=result(files_->seek(token,offset));
  if(!rc){rc=result(files_->read(token,bytes,want,&got));if(!rc&&got!=want){retain();rc=RISC_FILE_SOURCE_RETAINED;}}
  if(retained())return leave(RISC_FILE_SOURCE_RETAINED);
  const auto closed=close(token);if(closed)return leave(closed);
  rc=leave(rc);if(!rc){memcpy(out,bytes,got);*actual=got;}return rc;
 }
 int32_t discard() {
  const int32_t gate=enter();if(gate)return gate;
  if(active_){const int32_t rc=files_->remove(Name);if(rc!=RISC_FILES_OK&&rc!=RISC_FILES_NOT_FOUND)return leave(result(rc));}
  active_=sealed_=false;size_=written_=0;return leave(RISC_FILE_SOURCE_OK);
 }
 RiscBoot::FileSourceBackend backend() {
  return {this,[](void*c,uint64_t n){return static_cast<FileSourceFiles*>(c)->begin(n);},
   [](void*c,const void*b,uint32_t n,uint32_t*a){return static_cast<FileSourceFiles*>(c)->write(b,n,a);},
   [](void*c){return static_cast<FileSourceFiles*>(c)->finish();},
   [](void*c,uint64_t o,void*b,uint32_t n,uint32_t*a){return static_cast<FileSourceFiles*>(c)->readAt(o,b,n,a);},
   [](void*c){return static_cast<FileSourceFiles*>(c)->discard();},
   [](void*c){return static_cast<FileSourceFiles*>(c)->exitSafe();}};
 }
private:
 static constexpr const char* Name="/.risc-file-source";
 Hooks hooks_;RiscBoot::RuntimeFiles* files_=nullptr;uint64_t size_=0,written_=0;
 bool configured_=false,active_=false,sealed_=false,busy_=false,retained_=false;
 bool safe()const{return hooks_.owner&&hooks_.owner(hooks_.context)&&hooks_.safe&&hooks_.safe(hooks_.context);}
 void retain(){retained_=true;if(files_)files_->retainCustody();}
 static int32_t result(int32_t rc){switch(rc){case RISC_FILES_OK:return RISC_FILE_SOURCE_OK;case RISC_FILES_NO_SPACE:return RISC_FILE_SOURCE_NO_SPACE;case RISC_FILES_RETAINED:return RISC_FILE_SOURCE_RETAINED;case RISC_FILES_BUSY:return RISC_FILE_SOURCE_BUSY;case RISC_FILES_UNAVAILABLE:return RISC_FILE_SOURCE_UNAVAILABLE;default:return RISC_FILE_SOURCE_IO;}}
 int32_t enter(){if(retained())return RISC_FILE_SOURCE_RETAINED;if(!configured_)return RISC_FILE_SOURCE_UNAVAILABLE;if(busy_||!safe())return RISC_FILE_SOURCE_BUSY;busy_=true;return RISC_FILE_SOURCE_OK;}
 int32_t leave(int32_t rc){if(!safe()||rc==RISC_FILE_SOURCE_RETAINED)retain();busy_=false;return retained()?int32_t(RISC_FILE_SOURCE_RETAINED):rc;}
 int32_t space(uint64_t bytes){
  if(!hooks_.freeBytes)return RISC_FILE_SOURCE_OK;
  uint64_t available=0;
  if(!hooks_.freeBytes(hooks_.context,&available)){retain();return RISC_FILE_SOURCE_RETAINED;}
  return available<hooks_.headroom||bytes>available-hooks_.headroom?RISC_FILE_SOURCE_NO_SPACE:RISC_FILE_SOURCE_OK;
 }
 int32_t open(uint32_t flags,uint32_t*token){return result(files_->open(Name,flags,token));}
 int32_t inspect(uint32_t token,uint64_t size){uint64_t actual=0,at=0;const int32_t rc=result(files_->info(token,&actual,&at));if(rc)return rc;if(actual!=size){retain();return RISC_FILE_SOURCE_RETAINED;}return RISC_FILE_SOURCE_OK;}
 int32_t close(uint32_t token){const int32_t rc=files_->close(token);if(rc==RISC_FILES_OK)return RISC_FILE_SOURCE_OK;retain();return RISC_FILE_SOURCE_RETAINED;}
};
}
