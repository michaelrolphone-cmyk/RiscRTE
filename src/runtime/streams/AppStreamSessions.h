#pragma once
#include <RiscStreamClientV1.h>
#include "runtime/drivers/ProviderGraphV2.h"
namespace RuntimeStreams {
// Private app capability binding, copied from Runtime's authenticated ledger.
struct AppStreamBinding {
  uint64_t invocation=0;
  uint32_t slot=0, generation=0;
  const void* api=nullptr;
  RuntimeProviders::GrantV2 graph{};
};
class AppStreamSessions final {
 public:
  struct Host {
    void* context;
    bool (*valid)(void*,const AppStreamBinding&,bool active);
    void (*retain)(void*);
    void (*yield)(void*);
  };
  AppStreamSessions(RuntimeProviders::GraphV2& graph,Host host):graph_(graph),host_(host){}
  bool beginInvocation();
  void endInvocation() { context_=0; consumer_=0; }
  uint64_t context() const { return context_; }
  bool busy() const { return busy_; }
  bool retained() const;
  int32_t open(const AppStreamBinding&,const void*,uint32_t,uint32_t,risc_stream_opened_v1*);
  int32_t call(uint64_t,uint64_t,const void*,uint32_t,uint32_t,void*,uint32_t,uint32_t*);
  int32_t close(uint64_t,uint64_t,uint32_t);
  int32_t read(uint64_t,uint64_t,void*,uint32_t,uint32_t*);
  int32_t write(uint64_t,uint64_t,const void*,uint32_t,uint32_t*);
  int32_t info(uint64_t,uint64_t,risc_stream_client_info_v1*);
  bool closeGrant(const AppStreamBinding&);
  bool closeAll();
  void revokeAuthority();
 private:
  enum class State:uint8_t { Free,Reserved,Open,Closing,Retained };
  struct Session {
    State state=State::Free;
    bool authority=false;
    AppStreamBinding binding{};
    uint64_t providerContext=0,providerSession=0,session=0,rx=0,tx=0;
    uint32_t rxEndpoint=0,txEndpoint=0;
    const risc_stream_session_provider_v1* adapter=nullptr;
  } sessions_[16]{};
  RuntimeProviders::GraphV2& graph_;
  Host host_;
  uint64_t context_=0;
  uint32_t consumer_=0;
  bool busy_=false;
  bool valid(const Session&,bool active=true) const;
  Session* find(uint64_t,uint64_t,bool stream=false);
  int32_t fence(Session&);
  int32_t close(Session&,uint32_t,bool active);
  static uint64_t lease(const Session& s) { return (uint64_t(s.binding.graph.generation)<<32)|s.binding.graph.slot; }
};
}
