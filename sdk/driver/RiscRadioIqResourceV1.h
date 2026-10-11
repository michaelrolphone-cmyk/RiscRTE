#pragma once
/* Narrow CPU resource lease for the ESP32-S3 receive-only IQ dump driver.
 * The native implementation reserves the entire fixed bank before heap setup.
 * No modem registers or ROM calls are allowed until claim succeeds. */
#include <stdbool.h>
#include <stdint.h>
#define RISC_RADIO_IQ_RESOURCE_CAPABILITY "platform.radio.iq.resource"
#define RISC_RADIO_IQ_RESOURCE_API_V1 1u
typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    bool (*claim)(void *context, uint64_t *token);
    bool (*release)(void *context, uint64_t token);
    uint32_t bank_base, bank_bytes;
} risc_radio_iq_resource_v1;

/* Optional IQR2 suffix. Only the lease owner starts/stops the native worker.
 * tick runs in one native task and may only access the leased receiver and
 * provider-owned buffers. No Runtime/app/storage/display calls from tick.
 * stop joins before returning true; false pins the lease and mapped code. */
#define RISC_RADIO_IQ_WORKER_ABI 0x32525149u
typedef void (*risc_radio_iq_tick_v1)(void *,uint64_t monotonic_us);
typedef struct {
    risc_radio_iq_resource_v1 base;
    uint32_t worker_abi;
    bool (*start)(void *,uint64_t token,uint32_t interval_us,risc_radio_iq_tick_v1,void *);
    bool (*stop)(void *,uint64_t token);
    uint64_t (*now_us)(void *);
} risc_radio_iq_worker_resource_v1;
