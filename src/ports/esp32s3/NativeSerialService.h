#pragma once
#include "bootstrap/Runtime.h"
#include "SleepDiagnostics.h"

namespace RiscCpu { namespace NativeSerialFiles {
// Shared by the selected native owner loop and its production-path host test.
// Each poll is bounded. Repetition services frames larger than one RX budget;
// it does not admit application work or confuse queued TX with host receipt.
inline void serviceParkedOwner(RiscBoot::Runtime* runtime) {
  RiscDiagnostics::poll();
  if(runtime)runtime->nativeFilesCheckpoint();
  RiscDiagnostics::poll();
}
} }
