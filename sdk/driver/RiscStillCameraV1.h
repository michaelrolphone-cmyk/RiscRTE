#pragma once
#include <stdint.h>

// Product-owned, synchronous, single-frame backend. No framebuffer is borrowed.
// A null destination probes a frame and discards it. Zero means unavailable.
typedef struct {
  uint32_t api_version, struct_size;
  int32_t (*jpeg)(uint8_t* destination, uint32_t capacity);
} risc_still_camera_v1;
#define RISC_STILL_CAMERA_CAPABILITY "platform.camera-still"
