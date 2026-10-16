#include "NativeFileSource.h"
#if RISC_NATIVE_FILE_SOURCE && defined(ESP_PLATFORM)
#include "NativeAppData.h"
#include "NativeAppDataMount.h"
#include "NativeRadioResources.h"
#include "NativeScratchFiles.h"
#include "runtime/storage/FileSourceFiles.h"
extern "C" const uint32_t risc_file_source_abi __attribute__((used))=1;
namespace RiscFileSource { namespace {
bool(*ownerTask)()=nullptr;
bool(*operationSafe)()=nullptr;
bool attempted=false,configured=false,nativeRetained=false;
bool owner(){return ownerTask&&ownerTask();}
bool operation(){return owner()&&operationSafe&&operationSafe()&&RiscAppData::exitSafe();}
RiscCpu::NativeScratchFiles provider("/appdata/.risc-file-source");
RiscBoot::RuntimeFiles session;
bool providerSafe(){return owner()&&RiscAppDataMount::held()&&!RiscAppDataMount::retained()&&RiscCpu::nativeRadioResourceHeldReady();}
RiscStorage::FileSourceFiles files({nullptr,[](void*){return owner();},
 [](void*){return owner()&&RiscAppDataMount::held()&&!RiscAppDataMount::retained()&&RiscCpu::nativeRadioResourceHeldReady();},
 [](void*,uint64_t* out){size_t total=0,used=0;if(!out||!RiscAppDataMount::usage(&total,&used)||used>total)return false;*out=uint64_t(total-used);return true;},
 // A bounded reserve for ordinary app-data metadata/updates, not a guarantee
 // that another writer or every settings transaction will have enough space.
 UINT64_C(64)*1024,512,RiscCpu::NativeScratchFiles::maximumSize()});
// Native radio custody encloses the global real-mount guard. Neither lease is
// released after terminal storage custody. No failed close or unlink is retried.
template<class F> int32_t execute(F action) {
 if(!owner())return RISC_FILE_SOURCE_CONTEXT;
 if(nativeRetained||RiscAppDataMount::retained()||!files.exitSafe())return RISC_FILE_SOURCE_RETAINED;
 if(!configured)return RISC_FILE_SOURCE_UNAVAILABLE;
 if(!operation()||!RiscCpu::nativeRadioResourceTry())return RISC_FILE_SOURCE_BUSY;
 int32_t rc=RISC_FILE_SOURCE_BUSY;
 {
  RiscAppDataMount::Guard mount(RiscAppDataMount::Access::FileSource);
  if(mount.held)rc=action();
  if(rc==RISC_FILE_SOURCE_RETAINED||!files.exitSafe())RiscAppDataMount::retain();
 }
 if(RiscAppDataMount::retained()||!files.exitSafe()){nativeRetained=true;return RISC_FILE_SOURCE_RETAINED;}
 RiscCpu::nativeRadioResourceEnd();return rc;
}
const RiscBoot::FileSourceBackend api={nullptr,
 [](void*,uint64_t n){return execute([&](){return files.begin(n);});},
 [](void*,const void* b,uint32_t n,uint32_t* a){if(a)*a=0;return execute([&](){return files.write(b,n,a);});},
 [](void*){return execute([](){return files.finish();});},
 [](void*,uint64_t o,void* b,uint32_t n,uint32_t* a){if(a)*a=0;return execute([&](){return files.readAt(o,b,n,a);});},
 [](void*){return execute([](){return files.discard();});},
 [](void*){return exitSafe();}};
}
bool prepare(bool(*task)(),bool(*safe)()) {
 if(attempted)return false;
 attempted=true;ownerTask=task;operationSafe=safe;
 if(!operation()||!RiscAppDataMount::ready()||!RiscCpu::nativeRadioResourceTry())return false;
 {
  RiscAppDataMount::Guard mount(RiscAppDataMount::Access::FileSource);
  if(mount.held)configured=session.bind(provider.api(),owner,providerSafe)&&files.configure(&session,"/.risc-file-source");
  if(!files.exitSafe())RiscAppDataMount::retain();
 }
 if(RiscAppDataMount::retained()||!files.exitSafe()){nativeRetained=true;return false;}
 RiscCpu::nativeRadioResourceEnd();return configured;
}
const RiscBoot::FileSourceBackend* backend(){return configured?&api:nullptr;}
bool exitSafe(){return !nativeRetained&&!RiscAppDataMount::retained()&&files.exitSafe();}
bool resetSafe(){return exitSafe()&&!files.active();}
}
#endif
