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
