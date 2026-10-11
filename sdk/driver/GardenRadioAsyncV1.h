#pragma once
#include "GardenPlatformV1.h"
#include "RiscRadioAsyncV1.h"
typedef struct {
    garden_radio_v1 base;
    uint32_t async_tag, async_version;
    int32_t (*begin)(void*,uint64_t,const risc_radio_request_v1*,uint32_t*);
    int32_t (*poll)(void*,uint64_t,uint32_t,risc_radio_progress_v1*);
    int32_t (*cancel)(void*,uint64_t,uint32_t);
    /* Bounded owner service lease. Protects a direct app service call and its
     * nested native flash access; begin BUSY means do not call that service.
     * End the exact token in the same owner turn. Never spans yield or wait. */
    int32_t (*service_begin)(void*,uint64_t,uint32_t*);
    int32_t (*service_end)(void*,uint64_t,uint32_t);
} garden_radio_async_v1;
#define GARDEN_RADIO_ASYNC_V1_SIZE sizeof(garden_radio_async_v1)
