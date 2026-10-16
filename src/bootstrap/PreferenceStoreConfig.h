#pragma once
// Explicit compiled-in selection. Boot metadata cannot enable the broker.
#ifndef RISC_RUNTIME_PREFERENCE_STORE
#define RISC_RUNTIME_PREFERENCE_STORE 0
#endif
#if RISC_RUNTIME_PREFERENCE_STORE != 0 && RISC_RUNTIME_PREFERENCE_STORE != 1
#error RISC_RUNTIME_PREFERENCE_STORE must be 0 or 1
#endif
