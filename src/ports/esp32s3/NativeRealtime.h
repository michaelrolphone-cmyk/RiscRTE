#pragma once
#include <RiscRealtimeV1.h>
namespace RiscCpu { namespace NativeRealtime {
// Compiled-in boot owner only. Never register these functions as ELF imports.
void configure(bool (*owner)());
void start();
int32_t seed(int64_t epochSeconds,uint32_t nanoseconds);
int32_t read(risc_realtime_snapshot_v1*);
void enter(void (*terminal)());
}}
