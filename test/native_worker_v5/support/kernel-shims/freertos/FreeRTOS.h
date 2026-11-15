#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#define CONFIG_FREERTOS_SMP 0
#define CONFIG_FREERTOS_NUMBER_OF_CORES 2
#define CONFIG_FREERTOS_THREAD_LOCAL_STORAGE_POINTERS 1
#define CONFIG_FREERTOS_TLSP_DELETION_CALLBACKS 1
#define INCLUDE_xTaskGetIdleTaskHandle 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskDelay 1
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configMAX_TASK_NAME_LEN 16
#define configMAX_PRIORITIES 25
#define tskNO_AFFINITY (-1)
#define pdPASS 1
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef uint8_t StackType_t;
typedef struct { unsigned unused; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {0}
#ifdef __cplusplus
extern "C" {
#endif
void scope_test_enter(portMUX_TYPE *);
void scope_test_exit(portMUX_TYPE *);
BaseType_t xPortInIsrContext(void);
BaseType_t xPortGetCoreID(void);
#ifdef __cplusplus
}
#endif
#define portENTER_CRITICAL(p) scope_test_enter(p)
#define portEXIT_CRITICAL(p) scope_test_exit(p)
