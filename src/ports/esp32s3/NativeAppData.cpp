#if defined(ESP_PLATFORM) && defined(RISC_PAIRED_APP_DATA)
#include "NativeAppData.h"
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
RiscStorage::AsyncAppDataReplace*asyncWriter=nullptr;TaskHandle_t asyncTask=nullptr;
bool backgroundLease=false,asyncRetained=false;
bool pending(){return asyncWriter&&asyncWriter->pending();}
bool worker(){return !xPortInIsrContext()&&asyncTask&&xTaskGetCurrentTaskHandle()==asyncTask;}
bool (*ownerTask)()=nullptr;bool (*safeOperation)()=nullptr;bool attempted=false;
bool safe(){return ownerTask && ownerTask() && safeOperation && safeOperation();}
RiscStorage::AppDataFiles files({nullptr,[](void*){return uint32_t(esp_timer_get_time()/1000);},
 [](void*){vTaskDelay(1);return worker()||safe();},
 [](size_t size)->void*{return heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);},free});
void work(void*){for(;;){asyncWriter->workerStep();vTaskDelay(1);}}
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
   return files.replace(ns,name,expected,data,size);
  },nullptr);
 if(xTaskCreate(work,"appdata-writer",8192,nullptr,1,&asyncTask)==pdPASS)return true;
 asyncWriter->~AsyncAppDataReplace();asyncWriter=nullptr;asyncTask=nullptr;free(allocation);free(bytes);return false;
}
int32_t beginReplace(void*,uint32_t ns,const char*name,uint64_t expected,const void*bytes,uint32_t size,uint64_t*token){
 if(token)*token=0;
 if(!safe()||asyncRetained)return asyncRetained?RISC_APP_DATA_RETAINED:RISC_APP_DATA_UNAVAILABLE;
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
  if(*result==RISC_APP_DATA_RETAINED)asyncRetained=true;
  else if(backgroundLease){backgroundLease=false;RiscCpu::nativeBackgroundResourceEnd();}
 }
 return rc;
}
const RiscBoot::AppDataBackend api={nullptr,
 [](void*,uint32_t n,const char*p,uint32_t*s,uint64_t*r)->int32_t{if(s)*s=0;if(r)*r=0;if(asyncRetained)return RISC_APP_DATA_RETAINED;if(pending())return RISC_APP_DATA_BUSY;if(!safe())return RISC_APP_DATA_UNAVAILABLE;RiscCpu::NativeRadioResourceGuard lease;if(!lease.held)return RISC_APP_DATA_BUSY;return files.stat(n,p,s,r);},
 [](void*,uint32_t n,const char*p,uint64_t v,void*b,uint32_t c,uint32_t*s,uint64_t*r)->int32_t{if(s)*s=0;if(r)*r=0;if(asyncRetained)return RISC_APP_DATA_RETAINED;if(pending())return RISC_APP_DATA_BUSY;if(!safe())return RISC_APP_DATA_UNAVAILABLE;RiscCpu::NativeRadioResourceGuard lease;if(!lease.held)return RISC_APP_DATA_BUSY;return files.read(n,p,v,b,c,s,r);},
 [](void*,uint32_t n,const char*p,uint64_t v,const void*b,uint32_t s)->int32_t{if(asyncRetained)return RISC_APP_DATA_RETAINED;if(pending())return RISC_APP_DATA_BUSY;if(!safe())return RISC_APP_DATA_UNAVAILABLE;RiscCpu::NativeRadioResourceGuard lease;if(!lease.held)return RISC_APP_DATA_BUSY;return files.replace(n,p,v,b,s);},
 [](void*){return !asyncRetained&&(pending()||files.exitSafe());},beginReplace,collectReplace,
 [](void*){return pending();}};
}
bool prepare(bool (*owner)(),bool (*operationSafe)()){
 if(attempted)return false;
 attempted=true;ownerTask=owner;safeOperation=operationSafe;
 if(!safe())return false;
 const esp_partition_t*p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,esp_partition_subtype_t(0x41),"appdata");
 if(!p || p->encrypted || p->address!=0x270000 || p->size!=0x80000)return false;
 esp_vfs_littlefs_conf_t conf{};conf.base_path="/appdata";conf.partition_label="appdata";
 conf.format_if_mount_failed=false;conf.grow_on_mount=false;conf.read_only=false;conf.dont_mount=false;
 if(esp_vfs_littlefs_register(&conf)!=ESP_OK)return false;
 // Keep mount/provider state alive until reset, including allocation failure.
 return files.configure("/appdata");
}
const RiscBoot::AppDataBackend* backend(){return &api;}
bool exitSafe(){return !asyncRetained&&!pending()&&files.exitSafe();}
}
#endif
