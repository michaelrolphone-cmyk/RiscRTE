#pragma once
#include "runtime/provisioning/BootstrapInput.h"
#include <nvs.h>
#include <nvs_flash.h>
namespace RiscNvs {esp_err_t initializationStatus();}
namespace RiscBootstrap {
// Owner-provisioned blobs only. App KV namespaces are numeric rteXXXXXXXX and
// cannot name this namespace. Boot never creates, commits, repairs or erases it.
inline RiscProvision::Input nvsInput(){return {nullptr,
 [](void*,const char* key,void* bytes,uint32_t capacity,uint32_t* actual){
   using RiscProvision::InputStatus;if(actual)*actual=0;
   if(!key||!bytes||!capacity||!actual||RiscNvs::initializationStatus()!=ESP_OK)return InputStatus::Unavailable;
   nvs_handle_t handle=0;esp_err_t result=nvs_open("rte_bootstrap",NVS_READONLY,&handle);
   if(result==ESP_ERR_NVS_NOT_FOUND)return InputStatus::Missing;
   if(result!=ESP_OK)return InputStatus::Unavailable;
   size_t size=0;result=nvs_get_blob(handle,key,nullptr,&size);
   if(result==ESP_ERR_NVS_NOT_FOUND){nvs_close(handle);return InputStatus::Missing;}
   if(result!=ESP_OK||!size||size>capacity){nvs_close(handle);return InputStatus::Invalid;}
   const size_t expected=size;result=nvs_get_blob(handle,key,bytes,&size);nvs_close(handle);
   if(result!=ESP_OK||size!=expected)return InputStatus::Invalid;
   *actual=uint32_t(size);return InputStatus::Ready;
 }};}
}
