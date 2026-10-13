#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
/* Provider-only, device-scoped synchronous RGB565 backend. The broker owns
 * configured pins/controller. No app can acquire a platform capability.
 * Failed open may return a cleanup token; close must prove resource release. */
#define RISC_DISPLAY_BACKEND_CAPABILITY "platform.display.backend"
typedef struct {
 uint32_t api_version,struct_size;void* context;
 bool (*open)(void*,uint64_t*);
 bool (*row)(void*,uint64_t,uint32_t,const uint16_t*,size_t);
 bool (*brightness)(void*,uint64_t,uint8_t);
 bool (*close)(void*,uint64_t);
} risc_display_backend_v1;
