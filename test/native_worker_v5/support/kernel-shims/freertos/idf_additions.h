#pragma once
#include "freertos/task.h"
#ifdef __cplusplus
extern "C" {
#endif
TaskHandle_t xTaskGetIdleTaskHandleForCore(BaseType_t);
#ifdef __cplusplus
}
#endif
