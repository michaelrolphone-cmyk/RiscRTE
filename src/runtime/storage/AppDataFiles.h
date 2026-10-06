#pragma once
#include <RiscAppDataV1.h>
#include <cstdint>
#include <cstddef>
namespace RiscStorage {
/* Trusted port-local filesystem adapter. The root is mounted/provisioned by the
 * port; configure never creates it or formats anything. No descriptor is kept
 * after a successful call. Failed close latches retention, never retries. */
class AppDataFiles final {
 public:
  struct Hooks {
    void* context=nullptr;
    uint32_t (*now)(void*)=nullptr;
    bool (*cooperate)(void*)=nullptr;
    void* (*allocate)(size_t)=nullptr;
    void (*deallocate)(void*)=nullptr;
  };
  explicit AppDataFiles(Hooks hooks):hooks_(hooks){}
  bool configure(const char* root);
  bool retained()const{return retained_;}
  bool exitSafe()const{return !retained_ && !busy_;}
  bool ready()const{return ready_ && !retained_;}
  int32_t stat(uint32_t,const char*,uint32_t*,uint64_t*);
  int32_t read(uint32_t,const char*,uint64_t,void*,uint32_t,uint32_t*,uint64_t*);
  int32_t replace(uint32_t,const char*,uint64_t,const void*,uint32_t);
  static bool validName(const char*);
 private:
  static constexpr size_t PathMax=256;
  Hooks hooks_;
  char root_[160]{};
  bool ready_=false,retained_=false,busy_=false;
  uint32_t started_=0;
  uint64_t revision_=1;
  int32_t begin(uint32_t,const char*,char*,char*);
  bool cooperate();
  int32_t inspect(const char*,uint32_t*);
  int32_t close(int);
  int32_t verify(const char*,const void*,uint32_t);
  int32_t quota(const char*,const char*,uint32_t);
  int32_t removeStage(const char*);
};
}
