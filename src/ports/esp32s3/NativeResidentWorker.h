#pragma once
#include "runtime/workers/ResidentWorker.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_attr.h>
#include <cstdlib>
namespace RiscCpu { namespace NativeResidentWorker {
static bool(*ownerTask)()=nullptr;
static bool owner(){return !xPortInIsrContext()&&ownerTask&&ownerTask();}
static uint64_t now(){return uint64_t(esp_timer_get_time())/1000u;}
static void delay(uint32_t ms){const TickType_t ticks=pdMS_TO_TICKS(ms);vTaskDelay(ticks?ticks:1);}
struct Envelope {void(*run)(void*);void*argument;};
static void residentEntry(void*context){
 const Envelope envelope=*static_cast<Envelope*>(context);
 heap_caps_free(context); // Never consult allocation after callback completion.
 envelope.run(envelope.argument);
 // dispatch's completion store is its LAST descriptor access. This trampoline,
 // self-delete and deferred stack/TCB reclamation contain no provider callbacks.
 vTaskDelete(nullptr);std::abort();
}
static bool create(void(*run)(void*),void*argument,uint32_t stack,uint32_t priority,int32_t core){
 auto*envelope=static_cast<Envelope*>(heap_caps_malloc(sizeof(Envelope),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
 if(!envelope)return false;
 *envelope={run,argument};
 // Pinned nativeSDK6 task creation inserts into ready lists only after both
 // stack/TCB allocations succeed; failure cannot later dispatch the envelope.
 if(xTaskCreatePinnedToCore(residentEntry,"rte-worker",stack,envelope,priority,nullptr,core<0?tskNO_AFFINITY:core)==pdPASS)return true;
 heap_caps_free(envelope);return false;
}
// Static control atomics in internal DRAM, never caller/provider PSRAM.
static DRAM_ATTR RiscWorkers::ResidentWorker authority({owner,now,delay,create,configNUMBER_OF_CORES,configMAX_PRIORITIES});
static void configure(bool(*fn)()){ownerTask=fn;}
} }
