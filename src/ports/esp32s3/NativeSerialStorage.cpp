#include "NativeSerialStorage.h"
#if RISC_NATIVE_SERIAL_STORAGE_SELECTED
#if RISC_NATIVE_SERIAL_BOOT_FILES
#include "NativeSpiffsFiles.h"
#endif
#if RISC_NATIVE_SERIAL_APPDATA_FILES
#include "NativeAppDataMount.h"
#include "NativeRadioResources.h"
#include "runtime/storage/ReopenedFiles.h"
#endif
#include <new>
#if RISC_NATIVE_ESP_IDF
#include <esp_attr.h>
#endif
namespace RiscCpu { namespace NativeSerialStorage { namespace {
NativeSerialFiles::Check ownerCheck=nullptr,safeCheck=nullptr;
bool selected=false,prepared=false;
bool owner(){return ownerCheck&&ownerCheck();}
bool safe(){return owner()&&safeCheck&&safeCheck();}
#if RISC_NATIVE_SERIAL_APPDATA_FILES
bool resource=false;
bool enter(void*){
  if(!safe()||resource||!nativeBackgroundResourceTry())return false;
  resource=true;
  if(RiscAppDataMount::enter(RiscAppDataMount::Access::SerialFiles))return true;
  if(!RiscAppDataMount::retained()){resource=false;nativeBackgroundResourceEnd();}
  return false;
}
bool leave(void*){
  RiscAppDataMount::leave();
  if(RiscAppDataMount::retained())return false;
  if(resource){resource=false;nativeBackgroundResourceEnd();}
  return true;
}
// Reopened path/offset state is owner-task-only, not a DMA or atomic object.
// Keep mount/resource custody gates internal; preserve this provider through
// failed preparation or terminal retention, with no destruction or retry.
alignas(RiscStorage::ReopenedFiles)
#if RISC_NATIVE_ESP_IDF && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#if !CONFIG_SPIRAM_BOOT_INIT
#error "External serial provider requires SDK PSRAM boot initialization"
#endif
EXT_RAM_BSS_ATTR
#endif
unsigned char privateProviderStorage[sizeof(RiscStorage::ReopenedFiles)];
RiscStorage::ReopenedFiles& privateProvider(){
  return *std::launder(reinterpret_cast<RiscStorage::ReopenedFiles*>(privateProviderStorage));
}
#endif
bool ready(){return prepared;}
// Task-owned providers, clients and copied namespace tables never run in an ISR
// or a cache-disabled callback. Construct once after the owner/safety gates and
// SDK PSRAM boot initialization; retain every session after failed preparation.
struct SessionWorkspace {
#if RISC_NATIVE_SERIAL_BOOT_FILES
#ifdef RISC_EMBEDDED_BOOTSTORE
RiscStorage::PosixFiles bootProvider{"/bootfs",RiscStorage::PosixFiles::OpenBehavior::Posix,false};
#else
NativeSpiffsFiles bootProvider{"/bootfs"};
#endif
RiscBoot::RuntimeFiles bootClient;
risc_storage_files_v1 bootTable{};
#endif
#if RISC_NATIVE_SERIAL_APPDATA_FILES
RiscBoot::RuntimeFiles privateClient;
#endif
#if RISC_NATIVE_SERIAL_SD
RiscBoot::RuntimeFiles sdClient;
#endif
RiscSerial::Namespace namespaces[RISC_NATIVE_SERIAL_BOOT_FILES+RISC_NATIVE_SERIAL_APPDATA_FILES+RISC_NATIVE_SERIAL_SD]={
#if RISC_NATIVE_SERIAL_BOOT_FILES
  {1,"Active boot store",&bootClient,false},
#endif
#if RISC_NATIVE_SERIAL_APPDATA_FILES
  {2,"Serial files",&privateClient,true},
#endif
#if RISC_NATIVE_SERIAL_SD
  // Permanent wire namespace, deliberately independent of provider instance.
  {12,"SD card",&sdClient,true},
#endif
};
const NativeSerialFiles::Admission selectedAdmission{namespaces,sizeof(namespaces)/sizeof(namespaces[0]),ready};
};
alignas(SessionWorkspace)
#if RISC_NATIVE_ESP_IDF && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#if !CONFIG_SPIRAM_BOOT_INIT
#error "External serial sessions require SDK PSRAM boot initialization"
#endif
EXT_RAM_BSS_ATTR
#endif
unsigned char sessionStorage[sizeof(SessionWorkspace)];
SessionWorkspace& sessions(){return *std::launder(reinterpret_cast<SessionWorkspace*>(sessionStorage));}
} // namespace
bool prepare(NativeSerialFiles::Check ownerFn,NativeSerialFiles::Check safeFn){
  if(selected||!ownerFn||!safeFn||!ownerFn())return false;
  selected=true;ownerCheck=ownerFn;safeCheck=safeFn;
  if(!safe())return false;
  new (sessionStorage) SessionWorkspace;
#if RISC_NATIVE_SERIAL_BOOT_FILES
  sessions().bootTable=*sessions().bootProvider.api();
  sessions().bootTable.open=[](void*c,const char*p,uint32_t flags,uint32_t*out)->int32_t{
    if(out)*out=0;
    if(flags!=RISC_FILES_READ)return RISC_FILES_DENIED;
    return sessions().bootProvider.api()->open(c,p,flags,out);
  };
  sessions().bootTable.write=[](void*,uint32_t,const void*,uint32_t,uint32_t*out)->int32_t{if(out)*out=0;return RISC_FILES_DENIED;};
  sessions().bootTable.truncate=[](void*,uint32_t,uint64_t)->int32_t{return RISC_FILES_DENIED;};
  sessions().bootTable.mkdir=[](void*,const char*)->int32_t{return RISC_FILES_DENIED;};
  sessions().bootTable.rename=[](void*,const char*,const char*)->int32_t{return RISC_FILES_DENIED;};
  sessions().bootTable.remove=[](void*,const char*)->int32_t{return RISC_FILES_DENIED;};
  if(!sessions().bootClient.bind(&sessions().bootTable,owner,safe,sessions().bootProvider.identityApi(),sessions().bootProvider.directoryApi()))return false;
#endif
#if RISC_NATIVE_SERIAL_APPDATA_FILES
  new (privateProviderStorage) RiscStorage::ReopenedFiles({nullptr,enter,leave,
    [](void*){return RiscAppDataMount::retained();},[](void*){RiscAppDataMount::retain();},
    [](void*,uint64_t* total,uint64_t* used){size_t t=0,u=0;const bool ok=RiscAppDataMount::usage(&t,&u);*total=t;*used=u;return ok;}});
  if(!privateProvider().configure("/appdata/.serial-files"))return false;
  const int32_t provisioned=privateProvider().provision();
  if(provisioned==RISC_FILES_OK){
    if(!sessions().privateClient.bind(privateProvider().api(),owner,safe))return false;
  }else if(provisioned!=RISC_FILES_NO_SPACE && provisioned!=RISC_FILES_BUSY && provisioned!=RISC_FILES_UNAVAILABLE){
    return false; // Corruption/uncertain custody is not optional unavailability.
  }
  // Clean first-enable capacity/contention leaves this optional namespace
  // catalogued but unavailable. Do not block ordinary boot or retry mutation.
#endif
  prepared=true;return true;
}
const NativeSerialFiles::Admission* admission(){return prepared?&sessions().selectedAdmission:nullptr;}
#if RISC_NATIVE_SERIAL_SD
RiscBoot::RuntimeFiles& sdFiles(){return sessions().sdClient;}
#endif
} }
#endif
