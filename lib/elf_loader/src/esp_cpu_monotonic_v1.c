/* Resident boot-local clock. Never substitute a wrapping scheduler tick or
 * ambient UTC. ABI admission, not this function, controls who may import it. */
#include "RiscCpuMonotonicV1.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

int risc_cpu_monotonic_us_v1(uint64_t *out)
{
    if (!out) return RISC_CPU_MONOTONIC_INVALID;
    if (xPortInIsrContext() || !xTaskGetCurrentTaskHandle())
        return RISC_CPU_MONOTONIC_CONTEXT;
    const int64_t sample = esp_timer_get_time();
    if (sample < 0) return RISC_CPU_MONOTONIC_UNAVAILABLE;
    *out = (uint64_t)sample;
    return RISC_CPU_MONOTONIC_OK;
}
