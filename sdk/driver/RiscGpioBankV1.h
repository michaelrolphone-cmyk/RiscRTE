#pragma once
#include "RiscProviderV2.h"
#include "RiscLightSleepV1.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_GPIO_BANK_API_V1 1u
#define RISC_GPIO_BANK_CAPABILITY "gpio.bank"
enum {
    RISC_GPIO_INPUT = 1u << 0,
    RISC_GPIO_OUTPUT = 1u << 1,
    RISC_GPIO_PULLUP = 1u << 2,
    RISC_GPIO_PULLDOWN = 1u << 3
};
typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    void *context;
    bool (*claim)(void *context, uint8_t pin, uint32_t flags, uint64_t *claim_out);
    bool (*write)(void *context, uint64_t claim, bool level);
    bool (*read)(void *context, uint64_t claim, bool *level_out);
    bool (*release)(void *context, uint64_t claim);
    /* Validate this facade's input claim, translate to its private raw token;
     * never forward the public token unchanged. Retain it on RETAINED. */
    risc_gpio_light_sleep_v1 light_sleep;
} risc_gpio_bank_api_v1;
#define RISC_GPIO_BANK_LIGHT_SLEEP_V1_SIZE (offsetof(risc_gpio_bank_api_v1, light_sleep) + sizeof(((risc_gpio_bank_api_v1*)0)->light_sleep))
#ifdef __cplusplus
}
#endif
