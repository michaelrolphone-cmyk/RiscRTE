#pragma once
// Included inside RiscNvs, after Handle and the ordinary KV adapter.
namespace Settings {
constexpr uint32_t Maximum=RiscStorage::ScalableSettings::ValueMax+8;
inline esp_err_t open(Handle&h,uint32_t ns,const char*prefix,nvs_open_mode_t mode){
 char name[12];std::snprintf(name,sizeof name,"%s%08x",prefix,unsigned(ns));auto rc=nvs_open(name,mode,&h.value);h.opened=rc==ESP_OK;return rc;
}
inline int32_t readRaw(nvs_handle_t h,const char*k,void*out,uint32_t cap,uint32_t*n){
 *n=0;size_t size=0;auto rc=nvs_get_blob(h,k,nullptr,&size);if(rc==ESP_ERR_NVS_NOT_FOUND)return RISC_KEY_VALUE_NOT_FOUND;
 if(rc!=ESP_OK||!size||size>Maximum)return RISC_KEY_VALUE_IO;
 if(cap<size){*n=size;return RISC_KEY_VALUE_BUFFER_SMALL;}
 uint8_t bytes[Maximum];RiscRuntime::ScopedBufferWipe wipe(bytes);size_t actual=sizeof bytes;
 if(nvs_get_blob(h,k,bytes,&actual)!=ESP_OK||actual!=size)return RISC_KEY_VALUE_IO;
 std::memcpy(out,bytes,size);*n=size;return 0;
}
inline int32_t read(void*,uint32_t ns,void*out,uint32_t cap,uint32_t*n){
 if(!n||(!out&&cap)||!valid(ns,"state2"))return RISC_KEY_VALUE_INVALID;
 RiscCpu::NativeRadioResourceGuard lease;if(!lease.held)return RISC_KEY_VALUE_BUSY;
 if(initializationStatus()!=ESP_OK)return RISC_KEY_VALUE_IO;
 Handle h;auto rc=open(h,ns,"kvj",NVS_READONLY);if(rc==ESP_ERR_NVS_NOT_FOUND)return RISC_KEY_VALUE_NOT_FOUND;if(rc!=ESP_OK)return RISC_KEY_VALUE_IO;return readRaw(h.value,"state2",out,cap,n);
}
inline int32_t write(uint32_t ns,const char*prefix,const char*k,const void*data,uint32_t size){
 if(!valid(ns,k)||!data||!size||size>Maximum)return RISC_KEY_VALUE_INVALID;
 RiscCpu::NativeRadioResourceGuard lease;if(!lease.held)return RISC_KEY_VALUE_BUSY;
 if(initializationStatus()!=ESP_OK)return RISC_KEY_VALUE_IO;
 Handle h;if(open(h,ns,prefix,NVS_READWRITE)!=ESP_OK)return RISC_KEY_VALUE_IO;
 if(nvs_set_blob(h.value,k,data,size)!=ESP_OK||nvs_commit(h.value)!=ESP_OK)return RISC_KEY_VALUE_IO;
 Handle check;if(open(check,ns,prefix,NVS_READONLY)!=ESP_OK)return RISC_KEY_VALUE_IO;
 uint8_t bytes[Maximum];RiscRuntime::ScopedBufferWipe wipe(bytes);uint32_t n=0;
 if(readRaw(check.value,k,bytes,sizeof bytes,&n)||n!=size||std::memcmp(bytes,data,size))return RISC_KEY_VALUE_IO;return 0;
}
inline int32_t next(void*,uint32_t ns,bool delta,const char*after,char key[16],void*out,uint32_t cap,uint32_t*n){
 if(!after||!key||!n||!out||!valid(ns,"probe"))return RISC_KEY_VALUE_INVALID;*n=0;key[0]=0;
 RiscCpu::NativeRadioResourceGuard lease;if(!lease.held)return RISC_KEY_VALUE_BUSY;
 if(initializationStatus()!=ESP_OK)return RISC_KEY_VALUE_IO;
 const char*prefix=delta?"kvd":"rte";Handle h;auto rc=open(h,ns,prefix,NVS_READONLY);
 if(rc==ESP_ERR_NVS_NOT_FOUND)return RISC_KEY_VALUE_NOT_FOUND;if(rc!=ESP_OK)return RISC_KEY_VALUE_IO;
 char name[12];std::snprintf(name,sizeof name,"%s%08x",prefix,unsigned(ns));
 size_t used=0;if(nvs_get_used_entry_count(h.value,&used)!=ESP_OK)return RISC_KEY_VALUE_IO;
 auto it=nvs_entry_find("nvs",name,NVS_TYPE_ANY);if(!it&&used)return RISC_KEY_VALUE_IO;
 while(it){nvs_entry_info_t info{};nvs_entry_info(it,&info);
  if(std::strcmp(info.key,after)>0&&(!key[0]||std::strcmp(info.key,key)<0)){
   if(info.type!=NVS_TYPE_BLOB||!valid(ns,info.key)){nvs_release_iterator(it);return RISC_KEY_VALUE_IO;}
   std::strcpy(key,info.key);
  }
  it=nvs_entry_next(it);
 }
 if(!key[0])return RISC_KEY_VALUE_NOT_FOUND;return readRaw(h.value,key,out,cap,n);
}
inline int32_t erase(void*,uint32_t ns,const char*k){
 if(!valid(ns,k))return RISC_KEY_VALUE_INVALID;
 RiscCpu::NativeRadioResourceGuard lease;if(!lease.held)return RISC_KEY_VALUE_BUSY;
 if(initializationStatus()!=ESP_OK)return RISC_KEY_VALUE_IO;
 Handle h;auto rc=open(h,ns,"kvd",NVS_READWRITE);if(rc!=ESP_OK)return RISC_KEY_VALUE_IO;
 rc=nvs_erase_key(h.value,k);if(rc!=ESP_OK&&rc!=ESP_ERR_NVS_NOT_FOUND)return RISC_KEY_VALUE_IO;
 if(nvs_commit(h.value)!=ESP_OK)return RISC_KEY_VALUE_IO;
 size_t n=0;return nvs_get_blob(h.value,k,nullptr,&n)==ESP_ERR_NVS_NOT_FOUND?0:RISC_KEY_VALUE_IO;
}
inline const RiscStorage::ScalableSettings::Backend*backend(){
 static const RiscStorage::ScalableSettings::Backend api={nullptr,read,
  [](void*,uint32_t ns,const void*p,uint32_t n){return write(ns,"kvj","state2",p,n);},next,
  [](void*,uint32_t ns,const char*k,const void*p,uint32_t n){return write(ns,"kvd",k,p,n);},erase,
  [](void*,uint32_t ns,void*p,uint32_t cap,uint32_t*n){return RiscNvs::get(reinterpret_cast<void*>(uintptr_t(1)),ns,"snapshot",p,cap,n);}};return &api;
}
}
