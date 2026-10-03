#pragma once
#include "Board.h"
#include "runtime/drivers/ProviderGraphV2.h"
#include <RiscRuntimeV1.h>
namespace RiscBoot {
struct Port {
  bool (*owner)();
  bool (*health)(risc_runtime_health_v1*);
  void (*delay)(uint32_t);
  bool (*log)(const char*);
};
class Runtime final {
 public:
  explicit Runtime(Port p) : port_(p) {}
  bool prepare(const char* root);
  bool run();
  bool launch(const char* relative);
  bool health(risc_runtime_health_v1*);
  void yield(uint32_t);
  bool diagnostic(const char*);
  bool active() const { return active_ && port_.owner(); }
  const char* error() const { return error_; }
  Board& board() { return board_; }
 private:
  struct Driver {
    char id[96]{}, provides[96]{}, elf[256]{};
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
  Port port_;
  Board board_;
  RuntimeProviders::GraphV2 graph_;
  RuntimeProviders::GrantV2 grants_[16]{};
  Driver drivers_[16]{};
  size_t driverCount_=0, granted_=0;
  char root_[256]{}, default_[256]{}, current_[256]{}, queued_[256]{}, error_[192]{};
  bool prepared_=false, attempted_=false, active_=false, retained_=false;
};
}
