#pragma once
#include "FreeRTOS.h"
static inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* p) {
    return pthread_mutex_init(p, NULL) == 0 ? p : NULL;
}
static inline int xSemaphoreTake(SemaphoreHandle_t p, TickType_t ticks) {
    (void)ticks;
    return pthread_mutex_lock(p) == 0;
}
static inline int xSemaphoreGive(SemaphoreHandle_t p) {
    return pthread_mutex_unlock(p) == 0;
}
