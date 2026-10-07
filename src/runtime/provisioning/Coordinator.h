#pragma once
#include "Profile.h"
namespace RiscProvision {
enum class Step { Pending, Done, Failed };
enum class Selection { Selected, Unchanged, Unknown };
enum class State { Initial, Recover, Connect, Stage, Download, Validate, Close, Activate,
                   Cleanup, Installed, Recovery, Restart, Retained, SelectionUnknown };
// Private compiled-in boundary, NOT an ELF capability or native flash API.
// Profile and backend live until the coordinator reaches a terminal state.
// Calls run on the boot owner, before ANY driver/app starts; no pointers retained
// after terminal state. Every Pending call is bounded and yields to the caller.
struct Backend {
  void* context;
  uint32_t (*now)(void*);
  bool (*safe)(void*);
  // Recover abandoned staging; never mutate installed bytes/selector. Failed
  // cleanup means uncertain live resources, not permission to boot fallback.
  Step (*recover)(void*);
  bool (*installed)(void*); // validated committed store with known selector
  // Compare SHA256 of ALL exact profile bytes, computed by trusted caller, with
  // the digest stored atomically alongside the installed store's commit record.
  bool (*matches)(void*,const uint8_t*);
  Step (*connect)(void*,const char*,const char*);
  Step (*begin)(void*); // private empty staging, never active store
  Step (*download)(void*,const File&,uint32_t); // <= limit bytes/call, exact length
  // Read back EVERY file: exact inventory, length/SHA, board+whole boot graph,
  // ELF structure/imports; no driver/app execution. Must check backend capacity.
  Step (*validate)(void*,const Profile&);
  Step (*close)(void*); // release network and all handles before selection
  // Last mutation; commit digest AND store selector together, preserving old
  // store for boot-health rollback. Uncertain writes MUST return Unknown.
  Selection (*activate)(void*,const uint8_t*);
  Step (*abort)(void*); // idempotent cleanup, installed store remains untouched
};
class Coordinator {
 public:
  Coordinator(Backend b,const Profile& p,const uint8_t (&digest)[32]);
  State step();
  State state() const{return state_;}
  // No automatic reboot/launch: caller may do so only after terminal outcome.
  // SelectionUnknown permits only a safe reset / read-only reconciliation.
 private:
  bool complete() const;
  void fallback();
  Backend io_;const Profile& profile_;uint8_t digest_[32]{};
  State state_=State::Initial;uint32_t started_=0,cleanupStarted_=0;size_t file_=0;
};
}
