#if defined(ESP_PLATFORM) && defined(RISC_PAIRED_APP_DATA)
#include "NativeAppData.h"
#include "runtime/storage/AppDataFiles.h"
#include <esp_littlefs.h>
#include <esp_partition.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
namespace RiscAppData { namespace {
bool (*ownerTask)()=nullptr;bool (*safeOperation)()=nullptr;bool attempted=false;
bool safe(){return ownerTask && ownerTask() && safeOperation && safeOperation();}
RiscStorage::AppDataFiles files({nullptr,[](void*){return uint32_t(esp_timer_get_time()/1000);},
 [](void*){vTaskDelay(1);return safe();},
 [](size_t size)->void*{return heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);},free});
const RiscBoot::AppDataBackend api={nullptr,
 [](void*,uint32_t n,const char*p,uint32_t*s,uint64_t*r)->int32_t{if(s)*s=0;if(r)*r=0;return safe()?files.stat(n,p,s,r):RISC_APP_DATA_UNAVAILABLE;},
 [](void*,uint32_t n,const char*p,uint64_t v,void*b,uint32_t c,uint32_t*s,uint64_t*r)->int32_t{if(s)*s=0;if(r)*r=0;return safe()?files.read(n,p,v,b,c,s,r):RISC_APP_DATA_UNAVAILABLE;},
 [](void*,uint32_t n,const char*p,uint64_t v,const void*b,uint32_t s)->int32_t{return safe()?files.replace(n,p,v,b,s):RISC_APP_DATA_UNAVAILABLE;},
 [](void*){return files.exitSafe();}};
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
bool exitSafe(){return files.exitSafe();}
}
#endif
