#include "NativePackageVolume.h"
#if RISC_NATIVE_PACKAGE_VOLUME
#include "NativeAppData.h"
#include "NativeAppDataMount.h"
#include "NativeRadioResources.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "NativeStorageAdmission.h"
#include "runtime/storage/PackageFiles.h"
#include "runtime/storage/StorageWorker.h"
#include <RiscStorageAsyncAdapterV1.h>
#include <RiscInternalPackageVolumeV1.h>
#include <new>
#if !RISC_NATIVE_APP_DATA_MOUNT
#error Internal executable storage requires fenced NativeAppData mount custody
#endif
namespace RiscCpu {namespace NativeStorageWorker {
static bool isWorker(){return RiscAppData::onWorker();}
static void delayMs(uint32_t ms){vTaskDelay(pdMS_TO_TICKS(ms)?pdMS_TO_TICKS(ms):1);}
}}
namespace RiscPackageVolume {namespace {
bool(*ownerTask)()=nullptr;bool resource=false,prepared=false;
bool enter(void*);void leave(void*);
// Allocate the copied worker and adapter state from capability-checked internal
// RAM after startup. Static BSS must not intersect the reserved IQ SRAM bank.
struct Workspace {
 RiscStorage::Worker mailbox;
 risc_storage_async_adapter_v1 bridge{};
 RiscStorage::PackageFiles files;
 Workspace():files({nullptr,enter,leave,[](void*){return RiscAppDataMount::retained();},
  [](void*,uint64_t*t,uint64_t*u){size_t total=0,used=0;bool ok=RiscAppDataMount::usage(&total,&used);*t=total;*u=used;return ok;}}){}
};
Workspace*workspace=nullptr;
bool owner(){return ownerTask&&ownerTask();}
bool enter(void*){
 if(!RiscAppData::onWorker()&&(!owner()||workspace->bridge.session))return false;
 if(RiscAppDataMount::held()){RiscAppDataMount::Custody c{};return RiscAppDataMount::custody(c)&&c.access==RiscAppDataMount::Access::Packages;}
 if(!RiscCpu::nativeBackgroundResourceTry())return false;
 if(!RiscAppDataMount::enter(RiscAppDataMount::Access::Packages)){if(!RiscAppDataMount::retained())RiscCpu::nativeBackgroundResourceEnd();return false;}
 resource=true;return true;
}
void leave(void*){RiscAppDataMount::leave();if(resource&&!RiscAppDataMount::retained()){resource=false;RiscCpu::nativeBackgroundResourceEnd();}}
risc_storage_worker_admission_v2 workerApi{};
risc_storage_volume_api_v1_download volume{};
void pump(){if(workspace)workspace->mailbox.workerStep();}
bool initialize(){
 if(prepared)return true;
 if(!owner()||!RiscAppDataMount::ready())return false;
 void*memory=heap_caps_malloc(sizeof(Workspace),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);if(!memory)return false;
 workspace=new(memory)Workspace();
 auto discard=[](){workspace->~Workspace();free(workspace);workspace=nullptr;};
 if(!workspace->files.configure("/appdata/.packages")){discard();return false;}
 auto&w=workerApi.base.base;w.api_version=1;w.struct_size=sizeof(workerApi);
 w.submit=[](void*,const void*q,uint32_t n,uint64_t*t){return owner()?workspace->mailbox.submit(q,n,t):RISC_STORAGE_WORK_CONTEXT;};
 w.collect=[](void*,uint64_t t,void*out,uint32_t cap,uint32_t*n,int32_t*status){return owner()?workspace->mailbox.collect(t,out,cap,n,status):RISC_STORAGE_WORK_CONTEXT;};
 w.cancel=[](void*,uint64_t t){return owner()?workspace->mailbox.cancel(t):RISC_STORAGE_WORK_CONTEXT;};
 workerApi.base.admission_tag=RISC_STORAGE_ADMISSION_TAG;workerApi.base.admission_version=1;
 workerApi.base.application=[](void*,const risc_storage_volume_api_v1*v,const char*p,uint32_t n,const uint8_t*h,uint32_t*t){return RiscCpu::NativeStorageWorker::admitApplication(v,p,n,h,t);};
 workerApi.driver_tag=RISC_STORAGE_DRIVER_ADMISSION_TAG;workerApi.driver_version=1;
 workerApi.driver=[](void*,const risc_storage_volume_api_v1*v,const char*p,uint32_t n,const uint8_t*h,uint32_t*t){return RiscCpu::NativeStorageWorker::admitDriver(v,p,n,h,t);};
 auto&a=volume.base.base;a.fs=*risc_storage_volume_fs(workspace->files.api());auto&v=a.fs.state.prepared.base.sleep.terminal.power.volume.base;v.struct_size=sizeof(volume);
 a.async_tag=RISC_STORAGE_ASYNC_TAG;a.async_version=3;
 a.begin=[](void*,uint64_t*t)->int32_t{if(!owner())return RISC_STORAGE_ASYNC_INVALID;if(workspace->files.retained())return RISC_STORAGE_ASYNC_RETAINED;if(!workspace->files.exitSafe())return RISC_STORAGE_ASYNC_BUSY;return risc_storage_async_begin(&workspace->bridge,t);};
 a.submit=[](void*,uint64_t s,const risc_storage_async_request_v1*q,uint64_t*t){return owner()?risc_storage_async_submit(&workspace->bridge,s,q,t):RISC_STORAGE_ASYNC_INVALID;};
 a.collect=[](void*,uint64_t s,uint64_t t,risc_storage_async_result_v1*out){return owner()?risc_storage_async_collect(&workspace->bridge,s,t,out):RISC_STORAGE_ASYNC_INVALID;};
 a.end=[](void*,uint64_t s){return owner()?risc_storage_async_end(&workspace->bridge,s):RISC_STORAGE_ASYNC_INVALID;};
 volume.base.package_tag=RISC_STORAGE_PACKAGE_TAG;volume.base.package_version=1;
 volume.base.submit_package=[](void*,uint64_t s,const risc_storage_package_request_v1*q,uint64_t*t){return owner()?risc_storage_package_submit(&workspace->bridge,s,q,t):RISC_STORAGE_ASYNC_INVALID;};
 volume.download_tag=RISC_STORAGE_DOWNLOAD_TAG;volume.download_version=2;
 workspace->bridge.worker=&w;workspace->bridge.volume=&v;workspace->bridge.ext=risc_storage_volume_extension(&v);workspace->bridge.fs=&a.fs;
 workspace->bridge.package_free_bytes=[](void*c,uint64_t*out){bool ok=static_cast<RiscStorage::PackageFiles*>(c)->freeBytes(out);if(workspace->files.retained())workspace->bridge.retained=true;return ok;};
 if(workspace->mailbox.bind(risc_storage_async_execute,&workspace->bridge)!=RISC_STORAGE_WORK_OK||!RiscAppData::registerPackageWorker(pump)){discard();return false;}
 prepared=true;return true;
}
risc_internal_package_volume_v1 api={1,sizeof(api),nullptr,nullptr,[](void*){return exitSafe();}};
}
bool exitSafe(){return !prepared||(!workspace->bridge.retained&&!workspace->bridge.session&&workspace->mailbox.idle()&&workspace->files.exitSafe()&&!workspace->files.retained());}
bool bind(RiscBoot::Runtime&r,bool(*ownerFn)()){
 ownerTask=ownerFn;if(!initialize())return false;
 api.volume=&volume.base.base.fs.state.prepared.base.sleep.terminal.power.volume.base;
 return r.registerPlatform(RISC_INTERNAL_PACKAGE_VOLUME_CAPABILITY,1,RiscBoot::Runtime::Scope::Global,0,&api);
}
}
#endif
