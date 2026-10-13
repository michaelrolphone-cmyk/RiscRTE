#pragma once
/* Additive raw four-lane SPI transport. The v1 SPI prefix and one-lane operations
 * remain unchanged. Quad commands/panel registers belong to provider ELFs. */
#include "GardenPlatformV1.h"
#include "RiscSpiFrameV1.h"
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
    /* Append-only framed TX. Requires the advertised table extent below.
     * Payload is copied, 0..512 bytes; header-only commands are permitted.
     * Failure makes the active begin cleanup-only, including late completion. */
    bool (*exchange_frame)(void *,uint64_t token,const risc_spi_frame_v1 *,const uint8_t *,size_t);
} risc_spi_quad_v1;
#define RISC_SPI_QUAD_RAW_V1_SIZE offsetof(risc_spi_quad_v1,exchange_frame)
#define RISC_SPI_QUAD_FRAME_V1_SIZE sizeof(risc_spi_quad_v1)
