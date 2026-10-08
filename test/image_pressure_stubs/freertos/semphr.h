#pragma once
#include "../../support/native_registry/stubs/freertos/semphr.h"
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    static StaticSemaphore_t storage;
    return xSemaphoreCreateMutexStatic(&storage);
}
