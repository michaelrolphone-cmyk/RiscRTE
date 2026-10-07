#pragma once
#include <RiscRealtimeV1.h>

#define RISC_PLATFORM_REALTIME_CAPABILITY "platform.realtime"
#define RISC_PLATFORM_REALTIME_API_V1 1u

/* Read-only provider dependency, explicitly declared as platform.realtime@1.
 * This is exactly the canonical UTC snapshot/read ABI, with no seed operation.
 * Calls require the owner task, current Runtime, active app_main, live provider
 * generation and safe graph/native state. Calls during start, quiesce, stop,
 * app init/fini or outside app entry return CONTEXT without changing output.
 * Copying a table does not extend its lifetime; reacquisition gets a new token.
 * Providers may read during their bounded poll callback in active app entry.
 * There is no timezone conversion or hardware identity in this contract. */
typedef risc_realtime_api_v1 risc_platform_realtime_api_v1;
