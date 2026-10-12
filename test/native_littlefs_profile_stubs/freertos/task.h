#pragma once
#include "FreeRTOS.h"
static inline const char *pcTaskGetName(void *task) { (void)task; return "native-littlefs"; }
