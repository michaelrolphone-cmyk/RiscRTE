#pragma once
#include "runtime/sleep/RetainedWake.h"
namespace RiscCpu { namespace NativeRetainedWake {
RiscRetainedWake::Store* backend();
void start();
// Every non-deep reset is a cold session, irrespective of checkpoint validity.
bool coldBoot();
void enter(void (*terminal)());
}}
