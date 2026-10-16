#pragma once
#include <cstdint>
#include <memory>
namespace RiscBoot {
// Internal semantic boundary, not an application ABI. A provider session owns
// namespace/checkpoint state. Runtime owns policy, grants and receipt custody.
// Opening a candidate must return fresh state, never alias an active session.
class SettingsSession {
 public:
  virtual ~SettingsSession()=default;
  virtual uint32_t maxBlobSize() const=0;
  // Reads receive private Runtime scratch. Runtime validates status/size and
  // custody before exposing bytes. Failure must never imply fallback.
  virtual int32_t get(uint32_t,const char*,void*,uint32_t,uint32_t*,uint32_t,bool provider)=0;
  virtual int32_t put(uint32_t,const char*,const void*,uint32_t,bool provider)=0;
  virtual bool configure(uint32_t first,uint32_t count,bool checkpoints,const uint32_t* shared,uint32_t sharedCount)=0;
  virtual uint32_t checkpointCount() const=0;
  virtual uint32_t format() const=0;
  virtual uint32_t sharedCount() const=0;
  virtual uint32_t sharedNamespace(uint32_t) const=0;
  virtual uint32_t status(uint32_t slot) const=0;
  virtual int32_t restore(uint32_t,const char*,const void*,uint32_t,bool)=0;
  virtual int32_t snapshot(uint32_t,void*,uint32_t,uint32_t*,uint64_t*)=0;
  virtual int32_t acknowledge(uint32_t,uint64_t)=0;
  virtual bool launchReady(uint32_t,bool* pending) const=0;
  // Terminal custody is sticky and must not destroy session state.
  virtual bool retained() const {return false;}
};
struct SettingsProvider {
  void* context=nullptr;
  std::unique_ptr<SettingsSession> (*open)(void*)=nullptr;
};
}
