#pragma once
#include "bootstrap/Runtime.h"
#include "runtime/resources/ScopedBufferWipe.h"
#include <nvs.h>
#include <nvs_flash.h>
#include <cstdio>
#include <cstring>
namespace RiscNvs {
// Initialized once by Arduino's wrapped startup. Failure is latched without
// erasing/retrying/formatting the existing NVS partition.
esp_err_t initializationStatus();
inline bool valid(uint32_t id,const char* key) {
  if (!id || id>INT32_MAX || !key || !*key) return false;
  for (unsigned i=0;i<=RISC_KEY_VALUE_KEY_MAX;++i) {
    char c=key[i];if(!c)return true;
    if(i==RISC_KEY_VALUE_KEY_MAX || !((c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='.' || c=='-'))return false;
  }
  return false;
}
struct Handle {
  nvs_handle_t value=0; bool opened=false;
  ~Handle(){if(opened)nvs_close(value);}
  esp_err_t open(uint32_t id,nvs_open_mode_t mode) {
    char name[12];snprintf(name,sizeof(name),"rte%08x",static_cast<unsigned>(id));
    esp_err_t result=nvs_open(name,mode,&value);opened=result==ESP_OK;return result;
  }
};
inline int32_t get(void*,uint32_t id,const char* key,void* buffer,uint32_t capacity,uint32_t* outSize) {
  if(outSize)*outSize=0;
  if(!outSize || !valid(id,key) || (!buffer && capacity))return RISC_KEY_VALUE_INVALID;
  if(initializationStatus()!=ESP_OK)return RISC_KEY_VALUE_IO;
  Handle handle;esp_err_t result=handle.open(id,NVS_READONLY);
  if(result==ESP_ERR_NVS_NOT_FOUND)return RISC_KEY_VALUE_NOT_FOUND;
  if(result!=ESP_OK)return RISC_KEY_VALUE_IO;
  size_t size=0;result=nvs_get_blob(handle.value,key,nullptr,&size);
  if(result==ESP_ERR_NVS_NOT_FOUND)return RISC_KEY_VALUE_NOT_FOUND;
  if(result!=ESP_OK || !size || size>RISC_KEY_VALUE_V2_BLOB_MAX)return RISC_KEY_VALUE_IO;
  // Always validate the full read before returning a successful probe. Never
  // expose partial data even if the lower layer fails after copying bytes.
  uint8_t temp[RISC_KEY_VALUE_V2_BLOB_MAX];RiscRuntime::ScopedBufferWipe wipe(temp);size_t actual=sizeof(temp);
  result=nvs_get_blob(handle.value,key,temp,&actual);
  if(result!=ESP_OK || actual!=size)return RISC_KEY_VALUE_IO;
  if(capacity<size){*outSize=size;return RISC_KEY_VALUE_BUFFER_SMALL;}
  memcpy(buffer,temp,size);*outSize=size;return RISC_KEY_VALUE_OK;
}
inline int32_t put(void*,uint32_t id,const char* key,const void* data,uint32_t size) {
  if(!valid(id,key) || !data || !size || size>RISC_KEY_VALUE_V2_BLOB_MAX)return RISC_KEY_VALUE_INVALID;
  if(initializationStatus()!=ESP_OK)return RISC_KEY_VALUE_IO;
  Handle handle;if(handle.open(id,NVS_READWRITE)!=ESP_OK)return RISC_KEY_VALUE_IO;
  if(nvs_set_blob(handle.value,key,data,size)!=ESP_OK || nvs_commit(handle.value)!=ESP_OK)return RISC_KEY_VALUE_IO;
  // A separate read-only handle after commit checks the exact bytes. A reported
  // error can still mean data reached flash; do not erase/roll back/retry.
  uint8_t check[RISC_KEY_VALUE_V2_BLOB_MAX];RiscRuntime::ScopedBufferWipe wipe(check);uint32_t actual=0;
  if(get(nullptr,id,key,check,sizeof(check),&actual)!=RISC_KEY_VALUE_OK || actual!=size || memcmp(check,data,size))return RISC_KEY_VALUE_IO;
  return RISC_KEY_VALUE_OK;
}
inline const RiscBoot::KeyValueBackend* backend(){
  static const RiscBoot::KeyValueBackend value{nullptr,get,put,RISC_KEY_VALUE_V2_BLOB_MAX};return &value;
}
}
