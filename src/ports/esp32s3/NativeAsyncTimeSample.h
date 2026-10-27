#pragma once
#include "TimeSampleWorker.h"
#include "NativeInternalAtomics.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <new>
namespace RiscCpu { namespace NativeAsyncTimeSample {
static TaskHandle_t task=nullptr;
static TimeSampleWorker* mailbox=nullptr;
static bool (*owner)()=nullptr;
inline bool workerOwner(){return !xPortInIsrContext()&&task&&xTaskGetCurrentTaskHandle()==task;}
inline bool ready(){return mailbox&&NativeInternalAtomics::internal(*mailbox);}
inline void run(void*){for(;;){if(ready()&&mailbox->workPending()){mailbox->workerStep();vTaskDelay(1);}else ulTaskNotifyTake(pdTRUE,portMAX_DELAY);}}
inline bool prepare(bool(*o)(),bool(*networkReady)()){
 if(task)return ready();
 owner=o;NativeTimeSample::configure(workerOwner,networkReady);
 void* memory=heap_caps_malloc(sizeof(TimeSampleWorker),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
 if(!memory)return false;
 mailbox=new(memory) TimeSampleWorker(NativeTimeSample::api(),HttpTimeSample::now);
 if(!ready()){mailbox->~TimeSampleWorker();heap_caps_free(memory);mailbox=nullptr;return false;}
 // Permanent native task; a blocked SDK resolver is never force-deleted.
 if(xTaskCreate(run,"time-sample",8192,nullptr,1,&task)==pdPASS)return true;
 mailbox->~TimeSampleWorker();heap_caps_free(memory);mailbox=nullptr;task=nullptr;return false;
}
inline bool idle(){return !mailbox||(ready()&&mailbox->idle());}
inline bool safe(){return !mailbox||(ready()&&mailbox->safe());}
inline int32_t open(void*,const risc_time_sample_request_v1* r,uint64_t* out){if(out)*out=0;if(!owner||!owner())return RISC_TIME_SAMPLE_CONTEXT;if(!ready())return RISC_TIME_SAMPLE_CLOSED;const auto rc=mailbox->open(r,out);if(rc==RISC_TIME_SAMPLE_OK)xTaskNotifyGive(task);return rc;}
inline int32_t step(void*,uint64_t t,risc_time_sample_v1* out){if(!owner||!owner())return RISC_TIME_SAMPLE_CONTEXT;return ready()?mailbox->step(t,out):RISC_TIME_SAMPLE_CLOSED;}
inline int32_t close(void*,uint64_t t){if(!owner||!owner())return RISC_TIME_SAMPLE_CONTEXT;if(!ready())return RISC_TIME_SAMPLE_CLOSED;const auto rc=mailbox->close(t);if(rc==RISC_TIME_SAMPLE_PENDING)xTaskNotifyGive(task);return rc;}
inline const risc_time_sample_api_v1* api(){static const risc_time_sample_api_v1 value{1,sizeof(value),nullptr,open,step,close};return &value;}
} }
