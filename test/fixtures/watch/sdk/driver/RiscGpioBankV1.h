#pragma once
#include "RiscProviderV2.h"
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
} risc_gpio_bank_api_v1;
#ifdef __cplusplus
}
#endif
