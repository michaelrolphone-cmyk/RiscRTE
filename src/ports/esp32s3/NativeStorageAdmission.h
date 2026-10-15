#pragma once
#include "runtime/packages/ElfAdmission.h"
#include <RiscStorageWorkerAdmissionV1.h>
#include <private/elf_symbol.h>
#include "runtime/crypto/Sha256.h"
#include <esp_heap_caps.h>
#include <esp_timer.h>
extern "C" bool esp_elf_validate_file(const uint8_t*,size_t);
namespace RiscCpu { namespace NativeStorageWorker {
static bool isWorker();
static void delayMs(uint32_t);
static int32_t admitPackage(const risc_storage_volume_api_v1*v,const char*path,uint32_t size,const uint8_t*digest,uint32_t*retained,bool driver){
 if(retained)*retained=0;
 const auto*ext=risc_storage_volume_extension(v);
 if(!isWorker()||!retained||!ext||!ext->handle_error||!v->file_open_read||!v->file_read||!v->file_close||
    !path||!digest||!size||size>2097152u)return RISC_STORAGE_WORK_INVALID;
 auto*bytes=static_cast<uint8_t*>(heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(!bytes)return RISC_STORAGE_WORK_BUSY;
 const uint64_t started=uint64_t(esp_timer_get_time());
 uint64_t actual=0;uint32_t file=v->file_open_read(v->context,path,&actual);
 RiscCrypto::Sha256 hash;hash.initialize();
 bool ok=file&&actual==size&&hash.begin();
 uint32_t at=0;
 while(ok&&at<size){
  uint32_t want=size-at;if(want>4096)want=4096;
  size_t got=v->file_read(v->context,file,bytes+at,want);
  ok=got==want&&hash.add(bytes+at,got);
  at+=uint32_t(got);delayMs(1);
  if(uint64_t(esp_timer_get_time())-started>30000000u)ok=false;
 }
 uint8_t actualHash[32];ok=ok&&!ext->handle_error(v->context,file,false)&&hash.finish(actualHash)&&!memcmp(actualHash,digest,32);
 hash.release();
 if(file&&!v->file_close(v->context,file,false)){*retained=file;heap_caps_free(bytes);return RISC_STORAGE_WORK_RETAINED;}
 enum class Role {Application,Driver,Either};
 if(ok)ok=RiscPackages::admitElf(bytes,size,driver?Role::Driver:Role::Application,false,{esp_elf_validate_file,
  [](const char*name){return RiscPackages::ordinaryImport(name)&&esp_elf_public_import_supported_v1(name);},
  isWorker,[](){delayMs(1);},[]()->uint32_t{return uint32_t(esp_timer_get_time()/1000);}});
 heap_caps_free(bytes);return ok?RISC_STORAGE_WORK_OK:RISC_STORAGE_WORK_INVALID;
}
static int32_t admitApplication(const risc_storage_volume_api_v1*v,const char*p,uint32_t n,const uint8_t*h,uint32_t*r){return admitPackage(v,p,n,h,r,false);}
static int32_t admitDriver(const risc_storage_volume_api_v1*v,const char*p,uint32_t n,const uint8_t*h,uint32_t*r){return admitPackage(v,p,n,h,r,true);}
} }
