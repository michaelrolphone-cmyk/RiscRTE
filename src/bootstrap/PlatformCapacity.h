#pragma once

// Native product composition only. More table slots do not register a platform
// or grant any provider/application authority. Every table is still admitted.
#ifndef RISC_PLATFORM_CAPACITY
#define RISC_PLATFORM_CAPACITY 32
#endif
#if RISC_PLATFORM_CAPACITY != 32 && RISC_PLATFORM_CAPACITY != 40
#error "RISC_PLATFORM_CAPACITY must be 32 or 40"
#endif
#if RISC_PLATFORM_CAPACITY == 40
#define RISC_PLATFORM_CAPACITY_MARKER "RISC_PLATFORM_CAPACITY:40"
#else
#define RISC_PLATFORM_CAPACITY_MARKER "RISC_PLATFORM_CAPACITY:32"
#endif
