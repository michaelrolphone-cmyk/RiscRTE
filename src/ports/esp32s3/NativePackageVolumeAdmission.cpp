#include "NativePackageVolume.h"
#if RISC_NATIVE_PACKAGE_VOLUME
#include <RiscInternalPackageVolumeV1.h>
#include "runtime/storage/PackageFiles.h"
#include <esp_heap_caps.h>
#include <new>
namespace RiscPackageVolume {namespace {
struct Admission {
 RiscStorage::PackageFiles files;
 risc_storage_volume_api_v1_download package{};
 risc_internal_package_volume_v1 api{};
 Admission():files({nullptr,[](void*){return false;},[](void*){},[](void*){return false;},[](void*,uint64_t*,uint64_t*){return false;}}){}
 bool configure(){
  // configure() copies callback/path metadata only. Every media call is denied.
  if(!files.configure("/metadata-only-packages"))return false;
  auto&a=package.base.base;a.fs=*risc_storage_volume_fs(files.api());
  auto&v=a.fs.state.prepared.base.sleep.terminal.power.volume.base;v.struct_size=sizeof(package);
  a.async_tag=RISC_STORAGE_ASYNC_TAG;a.async_version=3;
  a.begin=[](void*,uint64_t*)->int32_t{return RISC_STORAGE_ASYNC_INVALID;};
  a.submit=[](void*,uint64_t,const risc_storage_async_request_v1*,uint64_t*)->int32_t{return RISC_STORAGE_ASYNC_INVALID;};
  a.collect=[](void*,uint64_t,uint64_t,risc_storage_async_result_v1*)->int32_t{return RISC_STORAGE_ASYNC_INVALID;};
  a.end=[](void*,uint64_t)->int32_t{return RISC_STORAGE_ASYNC_INVALID;};
  package.base.package_tag=RISC_STORAGE_PACKAGE_TAG;package.base.package_version=1;
  package.base.submit_package=[](void*,uint64_t,const risc_storage_package_request_v1*,uint64_t*)->int32_t{return RISC_STORAGE_ASYNC_INVALID;};
  package.download_tag=RISC_STORAGE_DOWNLOAD_TAG;package.download_version=2;
  api={1,sizeof(api),nullptr,&v,[](void*){return false;}};return true;
 }
};
// Keep substantial metadata outside scarce static internal/IQ SRAM.
Admission* admission=nullptr;
}
bool bindAdmission(RiscBoot::Runtime&r){
 if(!admission){
  void*memory=heap_caps_malloc(sizeof(Admission),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!memory)return false;
  auto*next=new(memory)Admission;
  if(!next->configure()){next->~Admission();free(next);return false;}
  admission=next;
 }
 return r.registerPlatform(RISC_INTERNAL_PACKAGE_VOLUME_CAPABILITY,1,RiscBoot::Runtime::Scope::Global,0,&admission->api);
}
}
#endif
