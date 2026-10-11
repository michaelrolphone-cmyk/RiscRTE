#pragma once
#include "NativeRadioAsync.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
namespace RiscCpu { namespace NativeRadioAsync {
// Startup allocation is outside begin/poll/cancel. The task and its stack live
// for firmware lifetime, including an indefinitely blocked vendor call.
// ESP-IDF uses bytes for xTaskCreate stack size. Hardware qualification must
// measure high-water on this 8 KiB budget before release.
constexpr uint32_t WorkerStackBytes=8192;
static TaskHandle_t workerTask=nullptr;
inline void task(void*){for(;;){step();vTaskDelay(pdMS_TO_TICKS(20)?pdMS_TO_TICKS(20):1);}}
inline bool provision(){
  if(ready)return true;
  if(workerTask)return false;
  if(!allocateBuffers())return false;
  if(xTaskCreate(task,"native-wifi",WorkerStackBytes,nullptr,1,&workerTask)!=pdPASS){workerTask=nullptr;releaseUnstartedBuffers();return false;}
  return provisioned();
}
} }
