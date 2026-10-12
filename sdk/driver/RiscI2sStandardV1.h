#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define RISC_I2S_STANDARD_CAPABILITY "platform.i2s.standard"
enum { RISC_I2S_TX=1u,RISC_I2S_RX=2u,RISC_I2S_DUPLEX=3u };
/* Pins/controller come from the bound hardware descriptor. Each frame is two
 * signed16 samples (left,right). Reads may succeed with a short/zero count.
 * Transfers copy at most 256 frames, with one 1..40 ms deadline. On failure,
 * a nonzero token remains owned until close succeeds; never replay partial TX.
 * No CPU, DMA or caller buffer remains borrowed after a transfer returns. */
typedef struct {
    uint32_t api_version,struct_size;
    void *context;
    bool (*open)(void *,uint8_t directions,uint32_t rate_hz,uint64_t *token);
    bool (*write)(void *,uint64_t,const int16_t *,size_t,size_t *,uint32_t);
    bool (*read)(void *,uint64_t,int16_t *,size_t,size_t *,uint32_t);
    bool (*close)(void *,uint64_t);
} risc_i2s_standard_api_v1;
