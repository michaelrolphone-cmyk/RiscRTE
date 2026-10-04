#pragma once
#include "bootstrap/Runtime.h"
#include <esp_heap_caps.h>
#include <new>
namespace RiscCpu {
/* Paired-target boot metadata only: ordinary owner-task reads, no ISR/DMA
 * buffers. Caller intentionally retains this object until reset, including
 * failed quiescence. Never fall back to the scarce internal TLS/DMA heap. */
inline RiscBoot::Runtime* createRetainedRuntime(RiscBoot::Port port) {
  void* memory=heap_caps_malloc(sizeof(RiscBoot::Runtime),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  return memory?new(memory)RiscBoot::Runtime(port):nullptr;
}
}
