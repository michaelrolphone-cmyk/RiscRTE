#pragma once
#include "bootstrap/RuntimeFiles.h"
#include <cstddef>
#include <cstdint>
namespace RiscSerial {
// Trusted firmware admission, never inferred from a physical backend or path.
// The immutable map, labels and provider sessions live until real restart.
struct Namespace {uint64_t id;const char* label;RiscBoot::RuntimeFiles* files;bool writable;};
struct Hooks {bool (*owner)();bool (*safe)();uint32_t (*millis)();};
class Files final {
 public:
  static constexpr size_t Chunk=48,Line=192,NamespaceMax=16;
  static constexpr uint32_t TimeoutMs=30000;
  // A healthy but unavailable storage boundary must answer before the public
  // client request deadline (5 s), without dispatching or claiming an outcome.
  static constexpr uint32_t AdmissionWaitMs=4000;
  Files()=default;
  Files(const Files&)=delete;Files& operator=(const Files&)=delete;
  bool bind(const Namespace*,size_t,Hooks);
  // Transport methods are RAM-only. No provider callback, allocation or wait.
  void input(uint8_t);
  void disconnect();
  const char* output()const{return response_+sent_;}
  size_t outputSize()const{return responseLength_-sent_;}
  void consumeOutput(size_t);
  // Only the settled owner boundary may dispatch provider calls. Each request
  // issues at most two bounded ABI operations, never an unbounded transfer loop.
  void checkpoint();
  // Owner-only RAM path: metadata replies and bounded admission refusal.
  // Never calls file providers, including close/cancel of a live handle.
  void controlCheckpoint();
  bool shutdown();
  bool exitSafe()const;
  bool custodySafe()const;
  bool pending()const{return (requestLength_>=6 && !strncmp(request_,"RTEF1 ",6)) || requestReady_ || outputSize() || handle_ || cancel_;}
 private:
  void command(bool storageReady=true);
  void reply(int32_t,const char* format=nullptr,...);
  int32_t close();
  void clearTransfer();
  void resetSession();
  const Namespace* find(uint64_t)const;
  void checkRetention();
  const Namespace* namespaces_=nullptr;size_t count_=0;Hooks hooks_{};
  const Namespace* selected_=nullptr;uint32_t handle_=0;
  uint64_t size_=0,offset_=0;uint32_t crc_=0xffffffffu,touched_=0;
  bool writing_=false,failed_=false,retained_=false,cancel_=false,bound_=false,calling_=false;
  char path_[RISC_STORAGE_FILES_PATH_MAX]{};size_t pathLength_=0;
  char request_[Line]{},previous_[Line]{},response_[Line]{},cached_[Line]{};
  size_t requestLength_=0,responseLength_=0,sent_=0;
  uint32_t session_=0,sequence_=0,replySession_=0,replySequence_=0;
  bool discard_=false,requestReady_=false;
};
}
