#pragma once
#include "runtime/sleep/RetainedWake.h"
namespace RiscCpu { namespace NativeRetainedWake {
RiscRetainedWake::Store* backend();
void start();
void enter(void (*terminal)());
}}
