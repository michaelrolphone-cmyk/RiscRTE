#include "OwnerNvsInstaller.h"
#ifdef RISC_PAIRED_BANKS
#include <nvs.h>
#include <nvs_flash.h>
namespace RiscNvs {esp_err_t initializationStatus();}
namespace RiscBootstrap {
RiscProvision::InstallResult installOwnerNvs(const void* profile,uint32_t size,const void* time,uint32_t timeSize,void* scratch,uint32_t capacity,bool (*safe)()){
 using namespace RiscProvision;
 struct Wipe {void* p;uint32_t n;~Wipe(){auto* b=static_cast<volatile unsigned char*>(p);if(b)while(n--)*b++=0;}} wipe{scratch,capacity};
 if(!scratch||capacity<ProfileInputBytes||!safe||!safe()||RiscNvs::initializationStatus()!=ESP_OK)return InstallResult::Unavailable;
 if(!validInstallInput(profile,size,time,timeSize))return InstallResult::InvalidInput;
 struct Context {nvs_handle_t handle;bool (*safe)();} context{0,safe};
 if(nvs_open("rte_bootstrap",NVS_READWRITE,&context.handle)!=ESP_OK)return InstallResult::Unavailable;
 InstallTransport transport{{&context,[](void* ptr,const char* key,void* bytes,uint32_t cap,uint32_t* actual){
   auto& c=*static_cast<Context*>(ptr);*actual=0;if(!c.safe())return InputStatus::Unavailable;
   size_t n=0;auto result=nvs_get_blob(c.handle,key,nullptr,&n);
   if(result==ESP_ERR_NVS_NOT_FOUND)return InputStatus::Missing;
   if(result!=ESP_OK||!n||n>cap)return InputStatus::Invalid;
   const auto expected=n;result=nvs_get_blob(c.handle,key,bytes,&n);
   if(result!=ESP_OK||n!=expected)return InputStatus::Invalid;
   *actual=uint32_t(n);return InputStatus::Ready;
 }},[](void* ptr,const char* key,const void* bytes,uint32_t n){auto& c=*static_cast<Context*>(ptr);return c.safe()&&nvs_set_blob(c.handle,key,bytes,n)==ESP_OK;},
 [](void* ptr){auto& c=*static_cast<Context*>(ptr);return c.safe()&&nvs_commit(c.handle)==ESP_OK;}};
 auto result=install(transport,profile,size,time,timeSize,scratch,capacity);nvs_close(context.handle);return result;
}
}
#endif
