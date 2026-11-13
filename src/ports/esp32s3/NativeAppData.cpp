#if defined(ESP_PLATFORM) && defined(RISC_PAIRED_APP_DATA)
#include "NativeAppData.h"
#include "NativeAppDataMount.h"
#include "NativeAppDataLayout.h"
#include "diagnostics/StageLog.h"
#include "NativeRadioResources.h"
#include "runtime/storage/AppDataFiles.h"
#include "runtime/storage/AsyncAppDataReplace.h"
#include <new>
#include <esp_littlefs.h>
#include <esp_partition.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
namespace RiscAppData { namespace {
#if RISC_NATIVE_APP_DATA_MOUNT
template<class F>int32_t mounted(RiscCpu::NativeRadioResourceGuard*lease,F operation){
 if(RiscAppDataMount::retained()){if(lease)lease->held=false;return RISC_APP_DATA_RETAINED;}
 int32_t result=RISC_APP_DATA_BUSY;
 {RiscAppDataMount::Guard custody(RiscAppDataMount::Access::AppData);
  if(!custody.held){
   if(RiscAppDataMount::retained()){if(lease)lease->held=false;return RISC_APP_DATA_RETAINED;}
   return RISC_APP_DATA_BUSY;
  }
  result=operation();
  if(result==RISC_APP_DATA_RETAINED)RiscAppDataMount::retain();
 }
 if(RiscAppDataMount::retained()){if(lease)lease->held=false;return RISC_APP_DATA_RETAINED;}
 return result;
}
#define APPDATA_CALL(lease,call) mounted(lease,[&](){return call;})
#define APPDATA_RETAINED (asyncRetained||RiscAppDataMount::retained())
#else
#define APPDATA_CALL(lease,call) (call)
#define APPDATA_RETAINED asyncRetained
#endif
RiscStorage::AsyncAppDataReplace*asyncWriter=nullptr;TaskHandle_t asyncTask=nullptr;
bool backgroundLease=false,asyncRetained=false;
bool pending(){return asyncWriter&&asyncWriter->pending();}
bool worker(){return !xPortInIsrContext()&&asyncTask&&xTaskGetCurrentTaskHandle()==asyncTask;}
bool (*ownerTask)()=nullptr;bool (*safeOperation)()=nullptr;bool attempted=false;
bool safe(){return ownerTask && ownerTask() && safeOperation && safeOperation();}
RiscStorage::AppDataFiles files({nullptr,[](void*){return uint32_t(esp_timer_get_time()/1000);},
 [](void*){vTaskDelay(1);return worker()||safe();},
 [](size_t size)->void*{return heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);},free});
#if RISC_NATIVE_PACKAGE_VOLUME
std::atomic<void(*)()> packagePump{nullptr};
void work(void*){for(;;){asyncWriter->workerStep();auto pump=packagePump.load(std::memory_order_acquire);if(pump)pump();vTaskDelay(1);}}
#else
void work(void*){for(;;){asyncWriter->workerStep();vTaskDelay(1);}}
#endif
bool prepareWriter(){
 if(asyncWriter)return true;
 void*bytes=heap_caps_malloc(RISC_APP_DATA_FILE_MAX,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!bytes)return false;
 void*allocation=heap_caps_malloc(sizeof(RiscStorage::AsyncAppDataReplace)+7,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
 if(!allocation){free(bytes);return false;}
 void*memory=reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(allocation)+7)&~uintptr_t(7));
 asyncWriter=new(memory)RiscStorage::AsyncAppDataReplace(bytes,RISC_APP_DATA_FILE_MAX,
  [](void*,uint32_t ns,const char*name,uint64_t expected,const void*data,uint32_t size)->int32_t{
   if(!worker())return RISC_APP_DATA_CONTEXT;
   return APPDATA_CALL(nullptr,files.replace(ns,name,expected,data,size));
  },nullptr);
 if(xTaskCreate(work,"appdata-writer",8192,nullptr,1,&asyncTask)==pdPASS)return true;
 asyncWriter->~AsyncAppDataReplace();asyncWriter=nullptr;asyncTask=nullptr;free(allocation);free(bytes);return false;
}
int32_t beginReplace(void*,uint32_t ns,const char*name,uint64_t expected,const void*bytes,uint32_t size,uint64_t*token){
 if(token)*token=0;
 if(!safe()||APPDATA_RETAINED)return APPDATA_RETAINED?RISC_APP_DATA_RETAINED:RISC_APP_DATA_UNAVAILABLE;
 if(pending())return RISC_APP_DATA_BUSY;
 if(!files.exitSafe())return RISC_APP_DATA_RETAINED;
 if(!prepareWriter())return RISC_APP_DATA_NO_SPACE;
 if(!RiscCpu::nativeBackgroundResourceTry())return RISC_APP_DATA_BUSY;
 backgroundLease=true;int32_t rc=asyncWriter->begin(ns,name,expected,bytes,size,token);
 if(rc!=RISC_APP_DATA_OK){backgroundLease=false;RiscCpu::nativeBackgroundResourceEnd();}
 return rc;
}
int32_t collectReplace(void*,uint64_t token,int32_t*result){
 if(!ownerTask||!ownerTask()||!asyncWriter)return RISC_APP_DATA_CONTEXT;
 int32_t rc=asyncWriter->collect(token,result);
 if(rc==RISC_APP_DATA_OK){
#if RISC_NATIVE_APP_DATA_MOUNT
  if(RiscAppDataMount::retained())*result=RISC_APP_DATA_RETAINED;
#endif
  if(*result==RISC_APP_DATA_RETAINED)asyncRetained=true;
  else if(backgroundLease){backgroundLease=false;RiscCpu::nativeBackgroundResourceEnd();}
 }
 return rc;
}
const RiscBoot::AppDataBackend api={nullptr,
 [](void*,uint32_t n,const char*p,uint32_t*s,uint64_t*r)->int32_t{if(s)*s=0;if(r)*r=0;if(APPDATA_RETAINED)return RISC_APP_DATA_RETAINED;if(pending())return RISC_APP_DATA_BUSY;if(!safe())return RISC_APP_DATA_UNAVAILABLE;RiscCpu::NativeRadioResourceGuard lease;if(!lease.held)return RISC_APP_DATA_BUSY;return APPDATA_CALL(&lease,files.stat(n,p,s,r));},
 [](void*,uint32_t n,const char*p,uint64_t v,void*b,uint32_t c,uint32_t*s,uint64_t*r)->int32_t{if(s)*s=0;if(r)*r=0;if(APPDATA_RETAINED)return RISC_APP_DATA_RETAINED;if(pending())return RISC_APP_DATA_BUSY;if(!safe())return RISC_APP_DATA_UNAVAILABLE;RiscCpu::NativeRadioResourceGuard lease;if(!lease.held)return RISC_APP_DATA_BUSY;return APPDATA_CALL(&lease,files.read(n,p,v,b,c,s,r));},
 [](void*,uint32_t n,const char*p,uint64_t v,const void*b,uint32_t s)->int32_t{if(APPDATA_RETAINED)return RISC_APP_DATA_RETAINED;if(pending())return RISC_APP_DATA_BUSY;if(!safe())return RISC_APP_DATA_UNAVAILABLE;RiscCpu::NativeRadioResourceGuard lease;if(!lease.held)return RISC_APP_DATA_BUSY;return APPDATA_CALL(&lease,files.replace(n,p,v,b,s));},
 [](void*){return !APPDATA_RETAINED&&(pending()||files.exitSafe());},beginReplace,collectReplace,
 [](void*){return pending();}};
}
bool prepare(bool (*owner)(),bool (*operationSafe)()){
 if(attempted){RISC_STAGE_LOG("appdata failed reason=already-attempted");return false;}
 attempted=true;ownerTask=owner;safeOperation=operationSafe;
 if(!safe()){RISC_STAGE_LOG("appdata failed reason=owner-or-provider-custody");return false;}
 const esp_partition_t*p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,esp_partition_subtype_t(0x41),"appdata");
 RISC_STAGE_LOG("appdata partition found=%u offset=%lu bytes=%lu expected_offset=%llu expected_bytes=%llu encrypted=%u",
   unsigned(p!=nullptr),static_cast<unsigned long>(p?p->address:0),static_cast<unsigned long>(p?p->size:0),
   static_cast<unsigned long long>(RiscNativeAppDataLayout::Offset),
   static_cast<unsigned long long>(RiscNativeAppDataLayout::Bytes),unsigned(p&&p->encrypted));
 if(!p || p->encrypted || !RiscNativeAppDataLayout::exact(p->address,p->size)){
  RISC_STAGE_LOG("appdata failed reason=partition-geometry-or-encryption");return false;
 }
 esp_vfs_littlefs_conf_t conf{};conf.base_path="/appdata";conf.partition_label="appdata";
 conf.format_if_mount_failed=false;conf.grow_on_mount=false;conf.read_only=false;conf.dont_mount=false;
#if RISC_NATIVE_APP_DATA_MOUNT
 RiscCpu::NativeRadioResourceGuard lease;if(!lease.held){RISC_STAGE_LOG("appdata failed reason=resource-custody");return false;}
 if(!RiscAppDataMount::prepare(p)){RISC_STAGE_LOG("appdata failed reason=mount-owner-admission");return false;}
 const esp_err_t mounted=esp_vfs_littlefs_register(&conf);
 RISC_STAGE_LOG("appdata mount result=%d format=disabled grow=disabled",int(mounted));
 const bool finished=mounted==ESP_OK&&RiscAppDataMount::finish();
 if(!finished){
  RISC_STAGE_LOG("appdata failed reason=%s retained=%u",mounted==ESP_OK?"mount-owner-finish":"littlefs-or-fence",unsigned(RiscAppDataMount::retained()));
  if(RiscAppDataMount::retained())lease.held=false;
  return false;
 }
 RiscAppDataMount::Guard custody(RiscAppDataMount::Access::AppData);
 if(!custody.held){RISC_STAGE_LOG("appdata failed reason=appdata-custody");return false;}
#else
 if(esp_vfs_littlefs_register(&conf)!=ESP_OK)return false;
#endif
 // Keep mount/provider state alive until reset, including allocation failure.
 const bool configured=files.configure("/appdata");
 RISC_STAGE_LOG("appdata configure result=%s",configured?"ok":"root-unavailable");
#if RISC_NATIVE_APP_DATA_MOUNT
 if(RiscAppDataMount::retained())lease.held=false;
#endif
 return configured;
}
#if RISC_NATIVE_PACKAGE_VOLUME
bool registerPackageWorker(void(*pump)()){if(!safe()||packagePump.load()||!pump||!prepareWriter())return false;packagePump.store(pump,std::memory_order_release);return true;}
bool onWorker(){return worker();}
#endif
const RiscBoot::AppDataBackend* backend(){return &api;}
bool custodySafe(){return !APPDATA_RETAINED&&(pending()||files.exitSafe());}
bool exitSafe(){return !APPDATA_RETAINED&&!pending()&&files.exitSafe();}
}
#endif
