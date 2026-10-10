#pragma once
#include <freertos/FreeRTOS.h>
extern bool resident_test_memory_lock;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void){
 static StaticSemaphore_t storage;return pthread_mutex_init(&storage,nullptr)==0?&storage:nullptr;
}
static inline int xSemaphoreTake(SemaphoreHandle_t handle,TickType_t){
 return resident_test_memory_lock && pthread_mutex_lock(handle)==0;
}
static inline int xSemaphoreGive(SemaphoreHandle_t handle){return pthread_mutex_unlock(handle)==0;}
