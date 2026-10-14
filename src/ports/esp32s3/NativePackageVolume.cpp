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
RiscStorage::Worker*mailbox=nullptr;
risc_storage_async_adapter_v1 bridge{};
bool owner(){return ownerTask&&ownerTask();}
bool enter(void*){
 if(!RiscAppData::onWorker()&&(!owner()||bridge.session))return false;
 if(RiscAppDataMount::held()){RiscAppDataMount::Custody c{};return RiscAppDataMount::custody(c)&&c.access==RiscAppDataMount::Access::Packages;}
 if(!RiscCpu::nativeBackgroundResourceTry())return false;
 if(!RiscAppDataMount::enter(RiscAppDataMount::Access::Packages)){if(!RiscAppDataMount::retained())RiscCpu::nativeBackgroundResourceEnd();return false;}
 resource=true;return true;
}
void leave(void*){RiscAppDataMount::leave();if(resource&&!RiscAppDataMount::retained()){resource=false;RiscCpu::nativeBackgroundResourceEnd();}}
RiscStorage::PackageFiles files({nullptr,enter,leave,[](void*){return RiscAppDataMount::retained();},
 [](void*,uint64_t*t,uint64_t*u){size_t total=0,used=0;bool ok=RiscAppDataMount::usage(&total,&used);*t=total;*u=used;return ok;}});
risc_storage_worker_admission_v2 workerApi{};
risc_storage_volume_api_v1_download volume{};
void pump(){if(mailbox)mailbox->workerStep();}
bool initialize(){
 if(prepared)return true;
 if(!owner()||!RiscAppDataMount::ready()||!files.configure("/appdata/.packages"))return false;
 void*memory=heap_caps_malloc(sizeof(RiscStorage::Worker),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);if(!memory)return false;
 mailbox=new(memory)RiscStorage::Worker();
 auto&w=workerApi.base.base;w.api_version=1;w.struct_size=sizeof(workerApi);
 w.submit=[](void*,const void*q,uint32_t n,uint64_t*t){return owner()?mailbox->submit(q,n,t):RISC_STORAGE_WORK_CONTEXT;};
 w.collect=[](void*,uint64_t t,void*out,uint32_t cap,uint32_t*n,int32_t*status){return owner()?mailbox->collect(t,out,cap,n,status):RISC_STORAGE_WORK_CONTEXT;};
 w.cancel=[](void*,uint64_t t){return owner()?mailbox->cancel(t):RISC_STORAGE_WORK_CONTEXT;};
 workerApi.base.admission_tag=RISC_STORAGE_ADMISSION_TAG;workerApi.base.admission_version=1;
 workerApi.base.application=[](void*,const risc_storage_volume_api_v1*v,const char*p,uint32_t n,const uint8_t*h,uint32_t*t){return RiscCpu::NativeStorageWorker::admitApplication(v,p,n,h,t);};
 workerApi.driver_tag=RISC_STORAGE_DRIVER_ADMISSION_TAG;workerApi.driver_version=1;
 workerApi.driver=[](void*,const risc_storage_volume_api_v1*v,const char*p,uint32_t n,const uint8_t*h,uint32_t*t){return RiscCpu::NativeStorageWorker::admitDriver(v,p,n,h,t);};
 auto&a=volume.base.base;a.fs=*risc_storage_volume_fs(files.api());auto&v=a.fs.state.prepared.base.sleep.terminal.power.volume.base;v.struct_size=sizeof(volume);
 a.async_tag=RISC_STORAGE_ASYNC_TAG;a.async_version=3;
 a.begin=[](void*,uint64_t*t)->int32_t{if(!owner())return RISC_STORAGE_ASYNC_INVALID;if(files.retained())return RISC_STORAGE_ASYNC_RETAINED;if(!files.exitSafe())return RISC_STORAGE_ASYNC_BUSY;return risc_storage_async_begin(&bridge,t);};
 a.submit=[](void*,uint64_t s,const risc_storage_async_request_v1*q,uint64_t*t){return owner()?risc_storage_async_submit(&bridge,s,q,t):RISC_STORAGE_ASYNC_INVALID;};
 a.collect=[](void*,uint64_t s,uint64_t t,risc_storage_async_result_v1*out){return owner()?risc_storage_async_collect(&bridge,s,t,out):RISC_STORAGE_ASYNC_INVALID;};
 a.end=[](void*,uint64_t s){return owner()?risc_storage_async_end(&bridge,s):RISC_STORAGE_ASYNC_INVALID;};
 volume.base.package_tag=RISC_STORAGE_PACKAGE_TAG;volume.base.package_version=1;
 volume.base.submit_package=[](void*,uint64_t s,const risc_storage_package_request_v1*q,uint64_t*t){return owner()?risc_storage_package_submit(&bridge,s,q,t):RISC_STORAGE_ASYNC_INVALID;};
 volume.download_tag=RISC_STORAGE_DOWNLOAD_TAG;volume.download_version=2;
 bridge.worker=&w;bridge.volume=&v;bridge.ext=risc_storage_volume_extension(&v);bridge.fs=&a.fs;
 bridge.package_free_bytes=[](void*c,uint64_t*out){bool ok=static_cast<RiscStorage::PackageFiles*>(c)->freeBytes(out);if(files.retained())bridge.retained=true;return ok;};
 if(mailbox->bind(risc_storage_async_execute,&bridge)!=RISC_STORAGE_WORK_OK||!RiscAppData::registerPackageWorker(pump))return false;
 prepared=true;return true;
}
risc_internal_package_volume_v1 api={1,sizeof(api),nullptr,nullptr,[](void*){return exitSafe();}};
}
bool exitSafe(){return !prepared||(!bridge.retained&&!bridge.session&&mailbox->idle()&&files.exitSafe()&&!files.retained());}
bool bind(RiscBoot::Runtime&r,bool(*ownerFn)()){
 ownerTask=ownerFn;if(!initialize())return false;
 api.volume=&volume.base.base.fs.state.prepared.base.sleep.terminal.power.volume.base;
 return r.registerPlatform(RISC_INTERNAL_PACKAGE_VOLUME_CAPABILITY,1,RiscBoot::Runtime::Scope::Global,0,&api);
}
}
#endif
