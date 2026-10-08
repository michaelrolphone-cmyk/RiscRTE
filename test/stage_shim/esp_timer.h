#pragma once
#include <cstdint>
inline int64_t timerUs=1234567;
inline unsigned timerCalls=0;
inline int64_t esp_timer_get_time(){++timerCalls;return timerUs++;}
