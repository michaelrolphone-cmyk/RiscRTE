#pragma once
#include "Board.h"
#include "runtime/drivers/ProviderGraphV2.h"
#include <RiscRuntimeV1.h>
#include <RiscKeyValueV1.h>
#include <RiscBoundKeyValueV1.h>
#include <RiscKeyValueV2.h>
#include <RiscBoundKeyValueV2.h>
namespace RiscBoot {
class Runtime;
// Optional compiled-in backend. Namespace comes only from validated boot policy.
// Backend must obey the public status/size contract; broker copies reads only
// after success. No app value encoding or fallback policy belongs here.
struct KeyValueBackend {
  void* context;
  int32_t (*get)(void*,uint32_t,const char*,void*,uint32_t,uint32_t*);
  int32_t (*put)(void*,uint32_t,const char*,const void*,uint32_t);
  uint32_t maxBlobSize=RISC_KEY_VALUE_BLOB_MAX;
};
struct Port {
  bool (*owner)();
  bool (*health)(risc_runtime_health_v1*);
  void (*delay)(uint32_t);
  bool (*log)(const char*);
  bool (*bindPlatforms)(Runtime&)=nullptr;
  const KeyValueBackend* keyValue=nullptr;
  // False means native resources must retain the invocation and provider graph.
  // This is a bounded non-mutating retention barrier, not provider quiescence.
  bool (*appExitSafe)()=nullptr;
  // Optional narrower native-retention barrier for bound provider storage.
  // Healthy RF may block app exit without revoking admitted provider KV.
  // If absent, retain the original appExitSafe behavior.
  bool (*providerStorageSafe)()=nullptr;
  // Explicit default-app health acknowledgement, never inferred from exit.
  bool (*confirmBoot)()=nullptr;
};
class Runtime final {
 public:
  static constexpr size_t MaxAppPolicies=16;
  explicit Runtime(Port p) : port_(p) {}
  ~Runtime() { revokeProviders(); }
  enum class Scope : uint8_t { Global, Device, Bus };
  // Compiled-in port registration only, never exported to apps/driver ELFs.
  // Tables/contexts must remain valid until successful runtime shutdown.
  bool registerPlatform(const char* capability, uint32_t api, Scope scope, uint64_t id, const void* table);
  bool prepare(const char* root);
  // Compiled-in pre-execution admission only. Enumerates the exact prepared
  // default, app-policy and driver image paths, with their expected entry role.
  // No module is mapped or executed. Duplicate instances share one inspection.
  bool inspectImages(bool (*inspect)(void*,const char* path,bool driver),void*) const;
  bool uses(uint64_t instance,const char* capability,uint32_t api) const;
  bool selected(uint64_t instance) const;
  bool run();
  bool launch(const char* relative);
  bool health(risc_runtime_health_v1*);
  bool acquire(const char*,uint32_t,uint64_t,risc_runtime_capability_v1*);
  bool release(risc_runtime_capability_v1*);
  void yield(uint32_t);
  bool diagnostic(const char*);
  bool confirmBoot();
  struct UpdateApp { char elf[193]{}, manifest[193]{}; };
  // Native update authority: preserve the existing boot-policy identity/grants.
  bool appUpdate(const char* id,const void* manifest,size_t size,UpdateApp&) const;
  size_t appCount() const {return policyCount_;}
  bool appInventory(size_t index,void*,size_t,uint32_t*) const;
  bool active() const { return active_ && port_.owner(); }
  bool retained() const { return retained_; }
  const char* error() const { return error_; }
  Board& board() { return board_; }
  const Board& board() const { return board_; }
 private:
  struct Driver {
    char id[96]{}, provides[96]{}, elf[256]{}, version[64]{};
    uint32_t api=0;
    uint64_t instance=0;
    RuntimeProviders::RequirementV2 requirements[16]{};
    char names[16][96]{};
    size_t count=0;
  };
  bool fail(const char* reason) { if (reason != error_) snprintf(error_,sizeof(error_),"%s",reason); return false; }
  bool manifest(JsonObjectConst, Driver&);
  bool validateGraph();
  bool runOne(const char*);
  bool appPolicies(JsonVariantConst);
  bool revokeApp();
  bool appExitBarrier();
  bool providerStorageSafe() const;
  bool appManifestPath(size_t,char*,size_t) const;
  static int32_t keyValueGet(void*,const char*,void*,uint32_t,uint32_t*);
  static int32_t keyValuePut(void*,const char*,const void*,uint32_t);
  struct ProviderKey {
    char key[RISC_BOUND_KEY_VALUE_KEY_MAX+1]{};
    uint32_t nameSpace=0;
    bool writable=false;
  };
  struct ProviderStorage {
    Runtime* owner=nullptr;
    ProviderKey keys[8]{};
    size_t count=0;
    risc_bound_key_value_v1 table{};
    bool live=false;
  };
  bool providerPolicy(JsonObjectConst,ProviderStorage&);
  void revokeProviders();
  static bool beginProvider(void*);
  static void revokeProvider(void*);
  static ProviderStorage* providerContext(void*);
  static int32_t boundKeyValueGet(void*,const char*,void*,uint32_t,uint32_t*);
  static int32_t boundKeyValuePut(void*,const char*,const void*,uint32_t);
  struct AppGrantPolicy {
    char capability[96]{}; uint32_t api=0; uint64_t instance=0;
    int driver=-1, platform=-1; bool keyValue=false;
  };
  struct AppPolicy {
    char id[96]{}, version[64]{}, elf[256]{};
    AppGrantPolicy grants[8]{}; size_t count=0;
  } policies_[MaxAppPolicies]{};
  size_t policyCount_=0;
  const AppPolicy* appPolicy_=nullptr;
  struct AppGrant {
    RuntimeProviders::GrantV2 provider{}; const void* api=nullptr;
    uint32_t generation=0; bool live=false;
    uint32_t keyValueNamespace=0; risc_key_value_v1 keyValue{};
  } appGrants_[16]{};
  uint32_t grantGeneration_=0;
  struct Platform {
    char capability[96]{}; uint32_t api=0; Scope scope=Scope::Global;
    uint64_t id=0; const void* table=nullptr;
  } platforms_[32]{};
  size_t platformCount_=0;
  Port port_;
  Board board_;
  // Must outlive graph destruction, including retained-module retry/abort.
  ProviderStorage providerStorage_[16]{};
  RuntimeProviders::GraphV2 graph_;
  RuntimeProviders::GrantV2 grants_[16]{};
  Driver drivers_[16]{};
  size_t driverCount_=0, granted_=0;
  char root_[256]{}, default_[256]{}, current_[256]{}, queued_[256]{}, error_[192]{};
  bool registrationOpen_=false;
  bool prepared_=false, attempted_=false, active_=false, retained_=false;
  bool defaultRunning_=false, entryRunning_=false;
};
}
