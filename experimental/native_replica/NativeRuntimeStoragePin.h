#pragma once
#include "ports/esp32s3/CpuPort.h"
#include <RiscStoragePackageAsyncV1.h>
#include <cstdlib>
#include <cstring>
#if !RISC_NATIVE_STORAGE_CUSTODY
#error NativeRuntimeStoragePin requires the explicitly selected native custody seam
#endif
namespace RiscExperimental {
// Compiled-in owner only. The selected installation volume comes from a complete
// admitted Runtime graph, never an application path/provider/namespace request.
// A pin is a graph grant, not a copied observer report. No SDK/export is added.
class NativeRuntimeStoragePin final {
 public:
 enum class State {Idle,Acquiring,Held,Releasing,Released,Retained};
 NativeRuntimeStoragePin(RiscBoot::Runtime&r,RiscCpu::Port&p,uint64_t device):runtime_(r),port_(p),device_(device){}
 ~NativeRuntimeStoragePin(){if(!exitSafe())std::abort();}
 NativeRuntimeStoragePin(const NativeRuntimeStoragePin&)=delete;NativeRuntimeStoragePin&operator=(const NativeRuntimeStoragePin&)=delete;
 int acquire(){
  auto&r=runtime_;
  if(!owner()||state_!=State::Idle)return RISC_STORAGE_ASYNC_INVALID;
  if(!device_||(!r.prepared_&&!r.active()
#if RISC_NATIVE_SETTINGS_LIFECYCLE
      // Runtime deliberately hides app authority during its exact native
      // checkpoint. That does not revoke this compiled-in graph owner's gate.
      &&!r.nativeCheckpointRunning_
#endif
     )||r.retained_||r.metadataCloseRetained_||r.retainedNativeProviderRead_||r.promotionRunning_||!r.graph_.activationSafe())return RISC_STORAGE_ASYNC_BUSY;
  if(!r.installation_||r.installation_[0].volume!=device_)return RISC_STORAGE_ASYNC_INVALID;
  // prepare publishes prepared_ before its final installed-registry allocation.
  // Require that transaction too; a partially admitted Runtime is not a pin.
  if(!r.installedMetadata_||!r.policies_||!r.fileHandlers_||r.policyCapacity_!=RiscBoot::Runtime::MaxAppPolicies)return RISC_STORAGE_ASYNC_BUSY;
  const RiscBoot::Runtime::Driver*selected=nullptr;
  for(size_t i=0;i<r.driverCount_;++i){const auto&d=r.drivers_[i];if(d.instance==device_&&d.api==1&&!std::strcmp(d.provides,"storage.volume")){if(selected)return RISC_STORAGE_ASYNC_INVALID;selected=&d;}}
  if(!selected)return RISC_STORAGE_ASYNC_INVALID;
  state_=State::Acquiring;
  grant_=r.graph_.acquireFrom(selected->id,"storage.volume",1,device_);
  if(!grant_.slot){if(!r.graph_.activationSafe()){r.retained_=true;state_=State::Retained;return RISC_STORAGE_ASYNC_RETAINED;}state_=State::Idle;return RISC_STORAGE_ASYNC_BUSY;}
  state_=State::Held;volume_=static_cast<const risc_storage_volume_api_v1*>(r.graph_.interfaceFor(grant_));
  if(!risc_storage_volume_download(volume_)){const int rc=release();return rc?rc:RISC_STORAGE_ASYNC_INVALID;}
  return RISC_STORAGE_ASYNC_OK;
 }
 bool owner()const{return port_.nativeStorageBoundTo(runtime_)&&runtime_.port_.owner&&runtime_.port_.owner();}
 bool held()const{return owner()&&state_==State::Held&&grant_.slot&&runtime_.graph_.holdsGrant(grant_)&&runtime_.graph_.interfaceFor(grant_)==volume_;}
 const risc_storage_volume_api_v1*volume()const{return held()?volume_:nullptr;}
 uint64_t device()const{return device_;}
 uint64_t token()const{return held()?(uint64_t(grant_.generation)<<32)|grant_.slot:0;}
 bool exitSafe()const{return !grant_.slot&&(state_==State::Idle||state_==State::Released);}
 State state()const{return state_;}
 int release(){
  if(!owner()||state_!=State::Held||!grant_.slot)return state_==State::Retained?RISC_STORAGE_ASYNC_RETAINED:RISC_STORAGE_ASYNC_INVALID;
  if(port_.nativeStorageAttached()||runtime_.graph_.lifecycleBusy())return RISC_STORAGE_ASYNC_BUSY;
  state_=State::Releasing;
  if(!runtime_.graph_.release(grant_)){runtime_.retained_=true;state_=State::Retained;return RISC_STORAGE_ASYNC_RETAINED;}
  grant_={};volume_=nullptr;state_=State::Released;return RISC_STORAGE_ASYNC_OK;
 }
 private:
 RiscBoot::Runtime&runtime_;RiscCpu::Port&port_;uint64_t device_;RuntimeProviders::GrantV2 grant_{};
 const risc_storage_volume_api_v1*volume_=nullptr;State state_=State::Idle;
};
}
