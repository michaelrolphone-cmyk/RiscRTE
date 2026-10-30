#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_DMA_RESERVATION_CAPABILITY "platform.dma-reservation"
enum {RISC_DMA_OK=0,RISC_DMA_INVALID=-1,RISC_DMA_CONTEXT=-2,RISC_DMA_CAPACITY=-3,
 RISC_DMA_PLATFORM=-4,RISC_DMA_STALE=-5,RISC_DMA_BUSY=-6};
/* Semantic routes, not vendor selector values. No RX or memory-to-memory grant. */
enum {RISC_DMA_SPI2=1,RISC_DMA_SPI3,RISC_DMA_UHCI0,RISC_DMA_I2S0,RISC_DMA_I2S1,
 RISC_DMA_LCD0,RISC_DMA_AES,RISC_DMA_SHA,RISC_DMA_RMT0};
enum {RISC_DMA_ESP32S3_AHB=1};
#define RISC_DMA_ROUTE_BIT(route) (UINT32_C(1)<<((route)-1))
#define RISC_DMA_ROUTES_ALL ((UINT32_C(1)<<RISC_DMA_RMT0)-1)
typedef struct {uint32_t struct_size,backend,group,channel;} risc_dma_channel_v1;
typedef void (*risc_dma_irq_handler_v1)(void*);
/* Device-scoped native authority. The shared SDK owns channel/trigger occupancy
 * and controller clocks. Selected board policy must explicitly authorize each
 * route for the validated device; instance scope alone is not route authority. No transfer engine, controller reset or RX sibling is
 * owned here. Only a successful reservation exposes a channel. A nonzero token
 * on failure owns partial resources and must be retained until release succeeds.
 *
 * IRQ sublease is route-matched, allocated disabled, non-shared/non-IRAM and
 * pinned to its creator task/core. It never exports a raw SDK interrupt handle.
 * irq_retire must observe actual disable AND free success; unknown failure
 * retains the IRQ and reservation. release refuses any remaining IRQ sublease
 * or unsafe/unknown owned channel state. No callback data may be freed before
 * successful irq_retire. This is not a promise about arbitrary external IRQs:
 * consumers must retire every independent ISR before returning their channel.
 * Channel bits alone do not establish external ISR completion. */
typedef struct {
 uint32_t api_version,struct_size;void*context;
 bool (*is_owner)(void*);
 int32_t (*reserve_tx)(void*,uint32_t,uint64_t*,risc_dma_channel_v1*);
 int32_t (*release)(void*,uint64_t);
 int32_t (*irq_claim)(void*,uint64_t,risc_dma_irq_handler_v1,void*);
 int32_t (*irq_enable)(void*,uint64_t,bool);
 int32_t (*irq_retire)(void*,uint64_t);
} risc_dma_reservation_api_v1;
#ifdef __cplusplus
}
#endif
