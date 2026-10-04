#pragma once
#include <pthread.h>
#include <stdint.h>
typedef pthread_mutex_t portMUX_TYPE;
typedef pthread_mutex_t StaticSemaphore_t;
typedef StaticSemaphore_t* SemaphoreHandle_t;
typedef uint32_t TickType_t;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define taskENTER_CRITICAL(p) ((void)pthread_mutex_lock(p))
#define taskEXIT_CRITICAL(p) ((void)pthread_mutex_unlock(p))
