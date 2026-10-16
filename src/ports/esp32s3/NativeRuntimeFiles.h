#pragma once
#include "NativeSpiffsFiles.h"
#include "bootstrap/RuntimeFiles.h"
#include <esp_heap_caps.h>
#include <esp_spiffs.h>
#include <new>
#ifdef RISC_EMBEDDED_BOOTSTORE
extern esp_err_t riscrte_mount_embedded_store();
#endif
namespace RiscCpu { namespace NativeRuntimeFiles {
// Optional native assembly for products which boot from a provisioned store.
// These backend roots are never interpreted by Runtime or application code.
// Sessions and provider state survive uncertain close until a real restart.
struct State {
#ifdef RISC_EMBEDDED_BOOTSTORE
 RiscStorage::PosixFiles active{"/bootfs",RiscStorage::PosixFiles::OpenBehavior::Posix,false};
#else
 NativeSpiffsFiles active{"/bootfs"};
#endif
 NativeSpiffsFiles staged{"/updatefs"};
 RiscBoot::RuntimeFiles activeClient,stagedClient;
 RiscBoot::RuntimeFileSources::Entry entries[2]{{"/bootfs",&activeClient},{"/updatefs",&stagedClient}};
 RiscBoot::RuntimeFileSources map{entries,2};
 bool (*owner)()=nullptr;bool (*safe)()=nullptr;
};
inline State*& state(){static State* value=nullptr;return value;}
inline const RiscBoot::RuntimeFileSources* sources(){return state()?&state()->map:nullptr;}
inline bool custodySafe(){return !state() || !state()->map.retained();}
inline bool exitSafe(){return !state() || (state()->activeClient.exitSafe() && state()->stagedClient.exitSafe() && state()->active.exitSafe() && state()->staged.exitSafe());}
inline bool prepare(bool(*owner)(),bool(*safe)()){
 if(!owner || !safe || !owner() || !safe())return false;
 if(state())return state()->owner==owner && state()->safe==safe && state()->activeClient.bound() && state()->stagedClient.bound() && custodySafe();
 void* memory=heap_caps_malloc(sizeof(State),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#if !defined(RISC_RUNTIME_METADATA_PSRAM) && !defined(RISC_PAIRED_BANKS)
 if(!memory)memory=heap_caps_malloc(sizeof(State),MALLOC_CAP_8BIT);
#endif
 if(!memory)return false;
 auto*s=new(memory)State;state()=s;s->owner=owner;s->safe=safe;
 return s->activeClient.bind(s->active.api(),owner,safe,s->active.identityApi(),s->active.directoryApi()) &&
   s->stagedClient.bind(s->staged.api(),owner,safe,s->staged.identityApi(),s->staged.directoryApi());
}
inline esp_err_t mountBoot(const char* partition){
#ifdef RISC_EMBEDDED_BOOTSTORE
 (void)partition;return riscrte_mount_embedded_store();
#else
 if(!partition)return ESP_ERR_INVALID_ARG;
 esp_vfs_spiffs_conf_t storage{};storage.base_path="/bootfs";storage.partition_label=partition;
 storage.max_files=4;storage.format_if_mount_failed=false;return esp_vfs_spiffs_register(&storage);
#endif
}
} }
