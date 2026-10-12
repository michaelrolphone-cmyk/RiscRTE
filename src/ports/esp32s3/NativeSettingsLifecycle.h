#pragma once
#include <cstddef>
#include <cstdint>
#ifndef RISC_NATIVE_SETTINGS_LIFECYCLE
#define RISC_NATIVE_SETTINGS_LIFECYCLE 0
#endif
#if RISC_NATIVE_SETTINGS_LIFECYCLE
#ifndef BOARD_XTEINK_X4_PRO
#error Native settings lifecycle is an explicitly selected X4-only integration
#endif
#if !RISC_NATIVE_SETTINGS_WORKER || !RISC_RUNTIME_ATOMIC_KEY_VALUE || !RISC_NATIVE_STORAGE_CUSTODY
#error Native settings lifecycle requires worker, atomic routing and storage custody selections
#endif
namespace RiscBoot { class Runtime; }
namespace RiscExperimental { class NativeReplicaCore; class CheckedNativeReplicaStore; class NativeSettingsCoordinator; }
namespace RiscCpu { class Port; namespace NativeSettingsLifecycle {
using Core=RiscExperimental::NativeReplicaCore;
using Store=RiscExperimental::CheckedNativeReplicaStore;
using Coordinator=RiscExperimental::NativeSettingsCoordinator;
struct Namespace { Core*core;Store*store; };
struct Writer {const char*app;uint32_t nameSpace;};
enum class Progress {Idle,Pending,Deferred,Failed,Retained};
// Compiled-in, independently qualified input. This is neither a filesystem
// factory nor a claim that pointer checks qualify a filesystem. Every referenced
// native object/control range must be permanent INTERNAL storage. Payload
// allocators, mount/provisioning, source admission and memory budgets are external
// prerequisites. There is no boolean approval, path, serialized permit or default.
struct Admission {
 Namespace preferences,wifi;
 const Writer*writers;size_t writerCount;
 void*context;size_t controlBytes;
 bool(*registerBeforeActivate)(void*,Coordinator&,Coordinator&);
 Progress(*begin)(void*,RiscBoot::Runtime&,Port&);
 Progress(*poll)(void*);
 void(*cancel)(void*);
 bool(*exitSafe)(void*);bool(*retained)(void*);
 // Optional separately qualified pre-app dispatcher, only after clean collected
 // Missing/Initializing load results. Receives exact results for both namespaces;
 // it must establish source/absence/receipt authority itself. Missing alone is
 // never FreshEmpty permission. Uses the same registered poll/cancel/custody
 // callbacks and permanent clients; no corrupt-present source is sent here.
 Progress(*beginPreApp)(void*,RiscBoot::Runtime&,Port&,int preferencesLoad,int wifiLoad)=nullptr;
 // Optional native RAM latch only, e.g. an already-admitted input/exit request.
 // No app callback, graph acquisition, input-device polling or storage I/O.
 bool(*cancelRequested)(void*)=nullptr;
};
struct Hooks {bool(*owner)();void(*schedulerDelay)(uint32_t);uint32_t(*millis)();bool(*cancelRequested)();};
struct Timing {uint64_t lastBlockedMs=0,maxBlockedMs=0;uint32_t lastPolls=0;bool cancellationRequested=false;};
// One process/firmware lifetime, no replace/unregister/destroy operation.
// Each namespace reserves one native-client slot for worker-only Core::load.
// Missing/initializing roots require a separate exact pre-app dispatcher.
// Corrupt roots refuse launch. The binding never initializes a baseline itself.
bool prepare(const Admission*,Hooks);
bool bind(RiscBoot::Runtime&); // before Runtime::prepare
bool hydrate(RiscBoot::Runtime&,Port&); // after graph prepare, before any app invocation
bool attachOwner(RiscBoot::Runtime&,Port&);
// A new attempt is admitted only at Runtime's explicit non-reentrant owner
// checkpoint. An accepted attempt is drained with scheduler-only waits, with
// no deadline, forced unwind, provider polling or app callback reentry.
bool checkpoint(bool admitNew=true);
void cooperate(); // drain only; ordinary display/input yields never start work
bool exitSafe();  // all namespaces, including complete-but-uncollected custody
bool retained();
bool ready();
Timing timing(); // RAM-only timing; no settings, keys, identities or fingerprints
} }
// An optional compiled-in selector supplies already-qualified permanent inputs.
// Absent selection fails closed. This getter must not mount or touch storage.
extern "C" const RiscCpu::NativeSettingsLifecycle::Admission* risc_native_settings_admission(void) __attribute__((weak));
#endif
