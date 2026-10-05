#pragma once
#include "CpuPort.h"
#include "runtime/provisioning/BootstrapInput.h"
namespace RiscBootstrap {
enum class TimeStatus {Ready,Pending,Unavailable};
struct TimeSample {
 uint64_t utc_seconds=0;
 uint64_t sampled_monotonic_ms=0; // same boot's hardware.now() domain
 uint32_t max_age_ms=0;           // bounded by 300000; never persisted/replayed
};
struct FreshTime {
 void* context=nullptr;
 // Trusted compiled-in source must establish genuinely current UTC. Pending
 // may acquire it using the already-connected network. No profile UTC fallback.
 TimeStatus (*poll)(void*,TimeSample*)=nullptr;
};
struct Port {
 RiscCpu::Hardware hardware;
 const RiscBoot::KeyValueBackend* keyValue=nullptr;
 bool (*operationSafe)()=nullptr;
 RiscProvision::Input input{};
 FreshTime time{};
};
enum class Outcome {Installed,Stopped};
enum class Reason {NoProfile,InvalidProfile,InputUnavailable,MemoryUnavailable,PairUnavailable,
                   Unchanged,ClockUnavailable,NetworkFailed,DownloadFailed,StageFailed,
                   CleanupRetained,NativeUnsafe,Activated,SelectionUnknown};
struct Result {Outcome outcome;Reason reason;};
// Paired setup only: selected store is already mounted/verified, Runtime not
// created/bound yet. Installed means continue NORMAL manifest/ELF admission.
// Stopped blocks app launch; selected or uncertain activation attempts only the
// existing safe restart and never falls through if it refuses/returns.
// Default returns no source. A compiled-in verified source may override this
// weak factory; credentials, URLs and time trust roots remain deployment input.
FreshTime configuredFreshTime();
Result run(const Port&,const char* installedRoot);
const char* reasonName(Reason);
}
