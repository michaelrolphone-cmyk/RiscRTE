#pragma once
#include <stdint.h>
extern int64_t admission_test_time;
static inline int64_t esp_timer_get_time(){return admission_test_time;}
