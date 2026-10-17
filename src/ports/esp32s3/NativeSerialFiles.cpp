#include "NativeSerialFiles.h"
#if RISC_NATIVE_SERIAL_FILES
#include "NativePlatform.h"
#include <cstring>
#include <new>
#if RISC_NATIVE_ESP_IDF
#include <esp_attr.h>
#endif
namespace RiscCpu { namespace NativeSerialFiles { namespace {
// Task-owned protocol bytes and copied admission metadata are never accessed
// by an ISR or a cache-disabled callback. Native PSRAM products reserve them in
// external BSS, independently of the fixed application/IQ banks and heap.
struct Workspace {
  RiscSerial::Files endpoint;
  RiscSerial::Namespace namespaces[RiscSerial::Files::NamespaceMax]{};
  char labels[RiscSerial::Files::NamespaceMax][64]{};
};
alignas(Workspace)
#if RISC_NATIVE_ESP_IDF && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#if !CONFIG_SPIRAM_BOOT_INIT
#error "External serial workspace requires SDK PSRAM boot initialization"
#endif
EXT_RAM_BSS_ATTR
#endif
unsigned char workspaceStorage[sizeof(Workspace)];
Workspace& workspace(){return *std::launder(reinterpret_cast<Workspace*>(workspaceStorage));}
// These gates remain internal and prevent access before construction. Selection
// is one-shot even on failed admission; a retained endpoint is never replaced.
struct State {
  Check owner=nullptr,platformSafe=nullptr,ready=nullptr;
  ControlObserver observer=nullptr;
  bool selected=false,prepared=false,dispatching=false;
} state;
bool owner(){return !xPortInIsrContext() && state.owner && state.owner();}
bool safe(){
  return owner() && state.dispatching && state.platformSafe && state.platformSafe() &&
    state.ready && state.ready();
}
struct Dispatch {
  Dispatch(){state.dispatching=true;}
  ~Dispatch(){state.dispatching=false;}
};
} // namespace
bool prepare(const Admission* admission,Check ownerCheck,Check platformSafe,ControlObserver observer){
  if(state.selected || !ownerCheck || !platformSafe || xPortInIsrContext() || !ownerCheck())return false;
  state.selected=true;state.owner=ownerCheck;state.platformSafe=platformSafe;state.observer=observer;
  if(!admission)return true;
  if(!admission->ready || admission->count>RiscSerial::Files::NamespaceMax ||
     (admission->count && !admission->namespaces))return false;
  // BSS does not supply Files' nonzero constructor defaults. Construct only at
  // the settled owner boundary, after SDK PSRAM startup, without allocation.
  new (workspaceStorage) Workspace;
  for(size_t i=0;i<admission->count;++i){
    const auto& source=admission->namespaces[i];
    if(!source.label || !*source.label || strnlen(source.label,sizeof(workspace().labels[i]))>=sizeof(workspace().labels[i]))return false;
    std::strcpy(workspace().labels[i],source.label);
    workspace().namespaces[i]={source.id,workspace().labels[i],source.files,source.writable};
  }
  state.ready=admission->ready;
  // bind performs copied metadata admission, never provider I/O. It does not
  // require a presently available provider or manufacture a storage session.
  Dispatch dispatch;
  state.prepared=workspace().endpoint.bind(workspace().namespaces,admission->count,
    {owner,safe,[]()->uint32_t{return RiscPlatform::millis();},
     [](uint32_t sid,uint32_t seq,const char*op,uint32_t wait,bool deadline){if(state.observer)state.observer(sid,seq,op,wait,deadline);}});
  return state.prepared;
}
bool checkpoint(){
  if(!state.prepared)return true;
  if(!owner())return false;
  if(state.dispatching)return workspace().endpoint.custodySafe();
  Dispatch dispatch;workspace().endpoint.checkpoint();return workspace().endpoint.custodySafe();
}
bool controlCheckpoint(){
  if(!state.prepared)return true;
  if(!owner())return false;
  if(state.dispatching)return workspace().endpoint.custodySafe();
  Dispatch dispatch;workspace().endpoint.controlCheckpoint();return workspace().endpoint.custodySafe();
}
bool shutdown(){
  if(!state.prepared)return true;
  if(!owner() || state.dispatching)return false;
  Dispatch dispatch;return workspace().endpoint.shutdown();
}
void input(uint8_t byte){if(state.prepared && owner() && !state.dispatching)workspace().endpoint.input(byte);}
void disconnect(){if(state.prepared && owner())workspace().endpoint.disconnect();}
const char* output(){return state.prepared && owner()?workspace().endpoint.output():nullptr;}
size_t outputSize(){return state.prepared && owner()?workspace().endpoint.outputSize():0;}
void consumeOutput(size_t bytes){if(state.prepared && owner())workspace().endpoint.consumeOutput(bytes);}
bool pending(){return state.prepared && (!owner() || workspace().endpoint.pending());}
bool exitSafe(){return !state.dispatching && (!state.prepared || (owner() && workspace().endpoint.exitSafe()));}
bool custodySafe(){return !state.prepared || (owner() && workspace().endpoint.custodySafe());}
} }
#endif
