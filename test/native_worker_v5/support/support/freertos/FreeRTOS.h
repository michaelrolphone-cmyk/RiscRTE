#pragma once
#include <stdint.h>
#include <pthread.h>
typedef void *TaskHandle_t;
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define taskENTER_CRITICAL(p) pthread_mutex_lock(p)
#define taskEXIT_CRITICAL(p) pthread_mutex_unlock(p)
