#pragma once
#include <cstdint>

#ifndef RISC_STAGE_LOGS
#define RISC_STAGE_LOGS 0
#endif

// Plain boundary statements, independent of the optional performance recorder.
// Disabled builds do not evaluate arguments or allocate formatting storage.
#if RISC_STAGE_LOGS
namespace RiscDiagnostics {
uint64_t monotonicUs();
void timestamped(const char* format,...) __attribute__((format(printf,1,2)));
}
#define RISC_STAGE_LOG(...) RiscDiagnostics::timestamped(__VA_ARGS__)
#else
#define RISC_STAGE_LOG(...) ((void)0)
#endif
