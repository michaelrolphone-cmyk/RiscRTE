#pragma once
#include "NativeReplicaCore.h"
#include <RiscStorageVolumeAsyncV1.h>
#include <cstdio>
namespace RiscExperimental {
// Bounded metadata proof, not a storage implementation or write permit. An
// INVALID READ_SMALL_FILE is ambiguous (empty/oversize/type); require the exact
// regular zero-size entry and complete enumeration followed by checked close.
// The consumer retains its existing session/custody and owns submit/collect.
class CheckedSdZeroFile final {
 public:
 using C=NativeReplicaCore;
 static constexpr uint32_t WorkMax=1048576;
 static bool supported(const risc_storage_volume_api_v1*volume){const auto*ext=risc_storage_volume_extension(volume);return ext&&volume->dir_open&&volume->dir_next&&ext->dir_close_checked&&ext->handle_error;}

 ~CheckedSdZeroFile(){C::wipe(this,sizeof *this);}
 void begin(uint32_t ns,const char*leaf){
  C::wipe(this,sizeof *this);std::snprintf(directory_,sizeof directory_,"/RTECFG/%08X",unsigned(ns));
  if(!leaf||std::strlen(leaf)>=sizeof leaf_||std::strchr(leaf,'/')){phase_=Done;error_=C::Invalid;return;}
  std::strcpy(leaf_,leaf);phase_=Open;
 }
 void request(risc_storage_async_request_v1&q)const{
  q={};q.operation=phase_==Open?RISC_STORAGE_ASYNC_OPEN_DIRECTORY:phase_==Next?RISC_STORAGE_ASYNC_NEXT_ENTRY:RISC_STORAGE_ASYNC_CLOSE_DIRECTORY;
  if(phase_==Open)std::strcpy(q.path,directory_);
 }
 int consume(int rc,const risc_storage_async_result_v1&r){
  if(phase_==Done)return error_;
  if(phase_==Next&&rc==RISC_STORAGE_ASYNC_END){phase_=Close;return C::Busy;}
  if(rc!=RISC_STORAGE_ASYNC_OK)return C::Conflict;
  if(phase_==Open){phase_=Next;return C::Busy;}
  if(phase_==Close){phase_=Done;return error_=found_?C::Ok:C::Conflict;}
  if(++work_>WorkMax)return C::NoSpace;
  const auto&e=r.entry;
  if(!std::memchr(e.name,0,sizeof e.name)||e.is_directory>1||std::strchr(e.name,'/')||std::strchr(e.name,'\\'))return C::Conflict;
  bool alias=true;for(unsigned i=0;i<sizeof e.name;++i){const char a=e.name[i],b=leaf_[i];const char lowerA=a>='A'&&a<='Z'?char(a-'A'+'a'):a,lowerB=b>='A'&&b<='Z'?char(b-'A'+'a'):b;if(lowerA!=lowerB){alias=false;break;}if(!a)break;}
  if(alias&&std::strcmp(e.name,leaf_))return C::Conflict;
  if(!std::strcmp(e.name,leaf_)){if(seen_)return C::Conflict;seen_=true;found_=!e.is_directory&&!e.size;}
  return C::Busy;
 }
 private:
 enum Phase {Done,Open,Next,Close};Phase phase_=Done;int error_=C::Invalid;uint32_t work_=0;bool seen_=false,found_=false;char directory_[32]{},leaf_[128]{};
};
}
