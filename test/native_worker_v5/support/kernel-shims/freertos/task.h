#pragma once
#include "freertos/FreeRTOS.h"
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
typedef void (*TlsDeleteCallbackFunction_t)(int, void *);
#ifdef __cplusplus
extern "C" {
#endif
TaskHandle_t xTaskGetCurrentTaskHandle(void);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char *, uint32_t, void *, UBaseType_t, TaskHandle_t *, BaseType_t);
void vTaskDelay(TickType_t);
void vTaskDelete(TaskHandle_t);
void *pvTaskGetThreadLocalStoragePointer(TaskHandle_t, BaseType_t);
void vTaskSetThreadLocalStoragePointerAndDelCallback(TaskHandle_t, BaseType_t, void *, TlsDeleteCallbackFunction_t);
void vTaskSetThreadLocalStoragePointer(TaskHandle_t, BaseType_t, void *);
#ifdef __cplusplus
}
#endif
