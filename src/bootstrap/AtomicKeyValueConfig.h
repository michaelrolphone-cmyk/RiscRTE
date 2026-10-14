#pragma once
// Experimental compiled-in broker. No default target or boot metadata enables it.
#ifndef RISC_RUNTIME_ATOMIC_KEY_VALUE
#define RISC_RUNTIME_ATOMIC_KEY_VALUE 0
#endif
#if RISC_RUNTIME_ATOMIC_KEY_VALUE != 0 && RISC_RUNTIME_ATOMIC_KEY_VALUE != 1
#error RISC_RUNTIME_ATOMIC_KEY_VALUE must be 0 or 1
#endif
