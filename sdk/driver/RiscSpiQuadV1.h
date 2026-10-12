#pragma once
/* Additive raw four-lane SPI transport. The v1 SPI prefix and one-lane operations
 * remain unchanged. Quad commands/panel registers belong to provider ELFs. */
#include "GardenPlatformV1.h"
#define RISC_SPI_QUAD_TAG 0x51535049u
#define RISC_SPI_QUAD_VERSION 1u
typedef struct {
    garden_spi_v1 base;
    uint32_t quad_tag, quad_version;
    bool (*claim_quad)(void *,uint8_t sclk,uint8_t data0,uint8_t data1,
                       uint8_t data2,uint8_t data3,uint8_t cs,uint64_t *token);
    /* TX only, 1..512 bytes, copied and drained under the current begin budget.
     * After failure only end/release may run; failed cleanup keeps its token. */
    bool (*exchange_quad)(void *,uint64_t token,const uint8_t *,size_t);
} risc_spi_quad_v1;
