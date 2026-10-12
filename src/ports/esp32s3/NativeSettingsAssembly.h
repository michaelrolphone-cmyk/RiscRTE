#pragma once
#ifndef RISC_NATIVE_SETTINGS_ASSEMBLY
#define RISC_NATIVE_SETTINGS_ASSEMBLY 0
#endif
#if RISC_NATIVE_SETTINGS_ASSEMBLY
#include "NativeSettingsBootstrap.h"
#include "NativeSettingsPayloadAllocator.h"
#if !RISC_NATIVE_SETTINGS_LIFECYCLE || !RISC_NATIVE_SETTINGS_PSRAM_PAYLOAD
#error Settings assembly requires explicit lifecycle and PSRAM payload selections
#endif
namespace RiscCpu { namespace NativeSettingsAssembly {
using C=RiscExperimental::NativeReplicaCore;
using Store=RiscExperimental::CheckedNativeReplicaStore;
using Artifacts=RiscExperimental::FreshInitArtifactInspector;
using Sources=RiscExperimental::CheckedNvsFreshSources;
using Files=RiscStorage::AppDataFiles;
// Every non-null callback context must have its whole permanent INTERNAL range
// supplied. Null plus zero is reserved for stateless compiled-in callbacks.
// Reachable controls, executable lifetimes, filesystem qualification and global
// lease exclusion remain the trusted backend supplier's contract.
struct Backend {
 Files*files=nullptr;Artifacts artifacts{};Sources*sources=nullptr;
 const char*root=nullptr; // compared in RAM only, not retained or opened
 Files::Hooks fileHooks{};size_t fileContextBytes=0;
 Store::Metadata metadata{};size_t metadataContextBytes=0;
 Store::Guard guard{};size_t guardContextBytes=0;
};
struct Namespace {
 Backend backend{};
 NativeSettingsBootstrap::Catalog::Digest digest{};size_t digestContextBytes=0;
 NativeSettingsBootstrap::First::Authority first{};size_t firstContextBytes=0;
 const uint8_t*initialIncarnation=nullptr; // copied; no entropy is generated here
};
struct Inputs {
 Namespace preferences,wifi;
 uint64_t device=0;
 const NativeSettingsLifecycle::Writer*writers=nullptr;size_t writerCount=0;
 bool(*owner)()=nullptr;
};
// Adapter for already provisioned private AppDataFiles. This is the exact same
// PSRAM-only payload allocator used by every owned Core/store/catalog client.
// Calling this does not configure, mount, allocate, initialize or access storage.
Files::Hooks fileHooks(void*context,uint32_t(*now)(void*),bool(*cooperate)(void*));
// One-shot, native-owner RAM-only construction. No worker registration, source
// inventory, baseline creation, backend callback, storage I/O or activation.
// Invalid selection is latched; retry/replacement/destruction is not supported.
// The caller and all supplied backend controls must remain alive permanently.
const NativeSettingsLifecycle::Admission*assemble(const Inputs&);
const NativeSettingsLifecycle::Admission*admission(); // null until assembled
// Native-only diagnostics and lifecycle test seams, not SDK imports.
NativeSettingsMaintenance*maintenance();
size_t controlBytes(); // static assembly only, excludes supplied controls/workers
} }
#endif
