#include "NativeSerialFiles.h"
#if RISC_NATIVE_SERIAL_FILES
#include "NativePlatform.h"
#include <cstring>
namespace RiscCpu { namespace NativeSerialFiles { namespace {
struct State {
  RiscSerial::Files endpoint;
  RiscSerial::Namespace namespaces[RiscSerial::Files::NamespaceMax]{};
  char labels[RiscSerial::Files::NamespaceMax][64]{};
  Check owner=nullptr,platformSafe=nullptr,ready=nullptr;
  bool selected=false,prepared=false,dispatching=false;
} state;
bool owner(){return state.owner && state.owner() && !xPortInIsrContext();}
bool safe(){
  return owner() && state.dispatching && state.platformSafe && state.platformSafe() &&
    state.ready && state.ready();
}
struct Dispatch {
  Dispatch(){state.dispatching=true;}
  ~Dispatch(){state.dispatching=false;}
};
} // namespace
bool prepare(const Admission* admission,Check ownerCheck,Check platformSafe){
  if(state.selected || !ownerCheck || !platformSafe || !ownerCheck() || xPortInIsrContext())return false;
  state.selected=true;state.owner=ownerCheck;state.platformSafe=platformSafe;
  if(!admission)return true;
  if(!admission->ready || admission->count>RiscSerial::Files::NamespaceMax ||
     (admission->count && !admission->namespaces))return false;
  for(size_t i=0;i<admission->count;++i){
    const auto& source=admission->namespaces[i];
    if(!source.label || !*source.label || strnlen(source.label,sizeof(state.labels[i]))>=sizeof(state.labels[i]))return false;
    std::strcpy(state.labels[i],source.label);
    state.namespaces[i]={source.id,state.labels[i],source.files,source.writable};
  }
  state.ready=admission->ready;
  // bind performs copied metadata admission, never provider I/O. It does not
  // require a presently available provider or manufacture a storage session.
  Dispatch dispatch;
  state.prepared=state.endpoint.bind(state.namespaces,admission->count,
    {owner,safe,[]()->uint32_t{return RiscPlatform::millis();}});
  return state.prepared;
}
bool checkpoint(){
  if(!state.prepared)return true;
  if(!owner() || state.dispatching)return state.endpoint.custodySafe();
  Dispatch dispatch;state.endpoint.checkpoint();return state.endpoint.custodySafe();
}
bool shutdown(){
  if(!state.prepared)return true;
  if(!owner() || state.dispatching)return false;
  Dispatch dispatch;return state.endpoint.shutdown();
}
void input(uint8_t byte){if(state.prepared && owner() && !state.dispatching)state.endpoint.input(byte);}
void disconnect(){if(state.prepared && owner())state.endpoint.disconnect();}
const char* output(){return state.prepared && owner()?state.endpoint.output():nullptr;}
size_t outputSize(){return state.prepared && owner()?state.endpoint.outputSize():0;}
void consumeOutput(size_t bytes){if(state.prepared && owner())state.endpoint.consumeOutput(bytes);}
bool pending(){return state.prepared && state.endpoint.pending();}
bool exitSafe(){return !state.dispatching && (!state.prepared || state.endpoint.exitSafe());}
bool custodySafe(){return !state.prepared || state.endpoint.custodySafe();}
} }
#endif
