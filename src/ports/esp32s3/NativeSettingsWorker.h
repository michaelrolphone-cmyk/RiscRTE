#pragma once
#ifndef RISC_NATIVE_SETTINGS_WORKER
#define RISC_NATIVE_SETTINGS_WORKER 0
#endif
#if RISC_NATIVE_SETTINGS_WORKER
#include "../../../experimental/native_replica/NativeSettingsCoordinator.h"
namespace RiscCpu { namespace NativeSettingsWorker {
// Explicit trusted-native opt-in only. No mount, NVS initialization, partition,
// graph/manifest selection or application callback registration happens here.
// Prepare before attaching SD. Referenced objects and all registered native
// client control blocks must be internal-RAM, permanent firmware objects.
using Coordinator=RiscExperimental::NativeSettingsCoordinator;
constexpr uint32_t StackBytes=8192,MaxBindings=2;
// Both registrations have the same exact owner task. Namespace, core backend
// and checked store must agree; duplicate namespaces/objects are rejected.
bool prepare(uint32_t namespaceId,RiscExperimental::NativeReplicaCore&,RiscExperimental::CheckedNativeReplicaStore&,bool(*owner)());
Coordinator*coordinator(uint32_t namespaceId); // exact owner task only
bool workerWithLease(uint32_t namespaceId);
bool isWorker(uint32_t namespaceId);
struct Custody {
 const Coordinator*coordinator=nullptr;const void*worker=nullptr;
 uint64_t generation=0;uint32_t nameSpace=0,holder=0;
};
// Worker-only, RAM-only snapshot of the real immutable registration and current
// accepted lease. Returns false before registration/acquisition, after release,
// on a different namespace/task or on retained custody. Never grants authority.
bool custody(uint32_t namespaceId,Custody*);
// Single-namespace compatibility. The no-argument lookups fail closed once a
// second binding exists; a store guard must use its exact namespace overload.
bool prepare(RiscExperimental::NativeReplicaCore&,RiscExperimental::CheckedNativeReplicaStore&,bool(*owner)());
Coordinator*coordinator();
bool workerWithLease();
bool isWorker();
// Aggregates EVERY registered namespace, including pending and Retained work.
bool exitSafe();
// Workers and control blocks are permanent, including blocked vendor calls.
// There is deliberately no stop, task-delete, unregister or release API.
} }
#endif
