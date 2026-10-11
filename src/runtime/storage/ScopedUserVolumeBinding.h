#pragma once
#include "ScopedUserVolume.h"
#include "runtime/drivers/ProviderGraphV2.h"
#include <optional>

namespace RiscStorage {
/* Trusted broker-side binding only. This class grants no application authority
 * and mounts/formats no filesystem. Its owner supplies an already-admitted
 * provider identity/instance and an explicit user-only subtree. Keep the graph,
 * binding and native custody hooks alive until end() succeeds. */
class ScopedUserVolumeBinding final {
 public:
  using Hooks=ScopedUserVolume::Hooks;
  ScopedUserVolumeBinding(RuntimeProviders::GraphV2& graph,Hooks hooks):graph_(graph),hooks_(hooks){}
  ScopedUserVolumeBinding(const ScopedUserVolumeBinding&)=delete;
  ScopedUserVolumeBinding& operator=(const ScopedUserVolumeBinding&)=delete;
  bool configure(const char* provider,uint64_t instance,const char* root,const char* label);
  bool begin(risc_storage_volume_api_v1_ext* out);
  bool end();
  bool retained() const { return retained_ || (volume_ && volume_->retained()); }
  bool releasePending() const { return grant_.slot && !volume_ && !retained_; }
  bool exitSafe() const { return !busy_ && !retained() && !grant_.slot && !volume_; }
  const char* error() const { return error_; }
 private:
  RuntimeProviders::GraphV2& graph_;
  Hooks hooks_;
  std::optional<ScopedUserVolume> volume_;
  RuntimeProviders::GrantV2 grant_{};
  const risc_storage_volume_api_v1* upstream_=nullptr;
  char provider_[96]{},root_[ScopedUserVolume::PathMax+1]{},label_[64]{},error_[128]{};
  uint64_t instance_=0;
  bool configured_=false,busy_=false,retained_=false;
  static bool owner(void*);
  static bool safe(void*);
  bool fail(const char*);
  bool retain(const char*);
  bool release();
};
}
