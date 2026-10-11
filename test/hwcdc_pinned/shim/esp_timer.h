#pragma once
#include "sdk.h"
inline int64_t esp_timer_get_time(){return int64_t(Stub::tick)*1000;}
