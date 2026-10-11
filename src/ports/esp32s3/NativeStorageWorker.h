#pragma once
#include "runtime/storage/StorageWorker.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <new>
namespace RiscCpu { namespace NativeStorageWorker {
// Allocated only for a selected platform.storage-worker dependency. Control
// atomics and copied buffers stay in INTERNAL RAM, never provider PSRAM.
// Permanent task: never delete a blocked vendor call or unload borrowed code.
static TaskHandle_t taskHandle=nullptr;
static bool prepare();
static bool isWorker(){return taskHandle&&xTaskGetCurrentTaskHandle()==taskHandle;}
static void delayMs(uint32_t ms){vTaskDelay(pdMS_TO_TICKS(ms)?pdMS_TO_TICKS(ms):1);}
static StorageWorkBackend backend{nullptr,prepare,isWorker,delayMs};
static bool idle(){return !backend.mailbox||backend.mailbox->idle();}
static void run(void*){for(;;){backend.mailbox->workerStep();delayMs(1);}}
static bool prepare(){
 if(taskHandle)return true;
 void*memory=heap_caps_malloc(sizeof(RiscStorage::Worker),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
 if(!memory)return false;
 backend.mailbox=new(memory)RiscStorage::Worker();
 if(xTaskCreate(run,"storage-worker",8192,nullptr,1,&taskHandle)==pdPASS)return true;
 backend.mailbox->~Worker();heap_caps_free(memory);backend.mailbox=nullptr;taskHandle=nullptr;return false;
}
} }
