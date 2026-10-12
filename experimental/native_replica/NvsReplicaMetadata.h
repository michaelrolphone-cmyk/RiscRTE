#pragma once
#include "CheckedNativeReplicaStore.h"
#include <nvs.h>
#include <cstdio>
namespace RiscExperimental {
// Optional experimental ESP-IDF-shaped metadata adapter. Does NOT initialize,
// erase, resize, format or enable NVS. The port supplies its existing init latch
// and proves worker custody under the shared native storage/radio lease.
class NvsReplicaMetadata {
 public:
 struct Hooks {void*context;bool(*workerWithLease)(void*);esp_err_t(*initializationStatus)(void*);};
 explicit NvsReplicaMetadata(Hooks hooks):hooks_(hooks){}
 CheckedNativeReplicaStore::Metadata api(){return {this,
  [](void*p,uint32_t ns,const char*k,void*out,uint32_t n){return static_cast<NvsReplicaMetadata*>(p)->read(ns,k,out,n);},
  [](void*p,uint32_t ns,const char*k,const void*data,uint32_t n){return static_cast<NvsReplicaMetadata*>(p)->write(ns,k,data,n);}};}
 private:
 using C=NativeReplicaCore;Hooks hooks_;
 struct Handle {nvs_handle_t value=0;bool open=false;~Handle(){if(open)nvs_close(value);}};
 struct Scratch {uint8_t*p;size_t n;~Scratch(){C::wipe(p,n);}};
 bool valid(uint32_t ns,const char*k,uint32_t n)const{return ns&&ns<=INT32_MAX&&k&&((!std::strcmp(k,"root")&&n==C::RootBytes)||(!std::strcmp(k,"next")&&n==24));}
 int ready()const{if(!hooks_.workerWithLease||!hooks_.workerWithLease(hooks_.context))return C::Busy;return hooks_.initializationStatus&&hooks_.initializationStatus(hooks_.context)==ESP_OK?C::Ok:C::Io;}
 static int map(esp_err_t rc){if(rc==ESP_OK)return C::Ok;if(rc==ESP_ERR_NVS_NOT_FOUND)return C::Missing;if(rc==ESP_ERR_NVS_NOT_ENOUGH_SPACE)return C::NoSpace;return C::Io;}
 static int open(Handle&h,uint32_t ns,nvs_open_mode_t mode){char name[12];std::snprintf(name,sizeof name,"rnr%08x",unsigned(ns));const auto rc=nvs_open(name,mode,&h.value);h.open=rc==ESP_OK;return map(rc);}
 int read(uint32_t ns,const char*k,void*out,uint32_t n){
  if(!out||!valid(ns,k,n))return C::Invalid;
  int rc=ready();if(rc)return rc;Handle h;rc=open(h,ns,NVS_READONLY);if(rc)return rc;
  size_t size=0;const auto found=nvs_get_blob(h.value,k,nullptr,&size);if(found!=ESP_OK)return map(found);if(size!=n)return C::Io;
  uint8_t bytes[C::RootBytes]{};Scratch wipe{bytes,sizeof bytes};size=sizeof bytes;
  if(nvs_get_blob(h.value,k,bytes,&size)!=ESP_OK||size!=n)return C::Io;
  std::memcpy(out,bytes,n);return C::Ok;
 }
 int write(uint32_t ns,const char*k,const void*data,uint32_t n){
  if(!data||!valid(ns,k,n))return C::Invalid;
  int rc=ready();if(rc)return rc;Handle h;rc=open(h,ns,NVS_READWRITE);if(rc)return rc;
  // Either API can report failure after persistence. No rollback or automatic
  // retry: the caller must re-read durable metadata before allocating another ID.
  auto status=nvs_set_blob(h.value,k,data,n);if(status!=ESP_OK)return map(status);
  status=nvs_commit(h.value);if(status!=ESP_OK)return map(status);
  uint8_t check[C::RootBytes]{};Scratch wipe{check,sizeof check};rc=read(ns,k,check,n);
  return rc?rc:std::memcmp(check,data,n)?C::Io:C::Ok;
 }
};
}
