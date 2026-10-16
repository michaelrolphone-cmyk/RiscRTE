#pragma once
#include <RiscKeyValueAtomicV1.h>
namespace RiscBoot {
// Internal, borrowed semantic endpoint. Registration copies this descriptor;
// the context and operations outlive Runtime and all retained receipts.
// Trusted providers obey the existing RiscKeyValueAtomicV1 status, size and
// output contracts. Direct atomic calls retain that contract; Runtime adds
// post-call owner/custody fencing. Legacy read-through separately stages bytes
// and validates size before publishing them to the caller.
struct AtomicSettingsEndpoint {
  struct Operations {
    uint32_t (*namespaceId)(void*);
    bool (*exitSafe)(void*);
    bool (*retained)(void*);
    int32_t (*state)(void*,risc_key_value_atomic_state_v1*);
    int32_t (*get)(void*,uint64_t,const char*,void*,uint32_t,uint32_t*);
    int32_t (*submit)(void*,uint64_t,const risc_key_value_atomic_mutation_v1*,uint32_t,const void*,uint32_t,uint64_t*);
    int32_t (*collect)(void*,uint64_t,risc_key_value_atomic_receipt_v1*);
    int32_t (*cancel)(void*,uint64_t);
  };
  void* context=nullptr;const Operations* operations=nullptr;
  explicit operator bool()const{return context&&operations&&operations->namespaceId&&operations->exitSafe&&operations->retained&&operations->state&&operations->get&&operations->submit&&operations->collect&&operations->cancel;}
  bool same(const AtomicSettingsEndpoint& other)const{return context==other.context&&operations==other.operations;}
  uint32_t namespaceId()const{return operations->namespaceId(context);}
  bool exitSafe()const{return operations->exitSafe(context);}
  bool retained()const{return operations->retained(context);}
  int32_t state(risc_key_value_atomic_state_v1* out)const{return operations->state(context,out);}
  int32_t get(uint64_t rev,const char*key,void*out,uint32_t cap,uint32_t*size)const{return operations->get(context,rev,key,out,cap,size);}
  int32_t submit(uint64_t rev,const risc_key_value_atomic_mutation_v1* m,uint32_t n,const void*intent,uint32_t size,uint64_t*token)const{return operations->submit(context,rev,m,n,intent,size,token);}
  int32_t collect(uint64_t token,risc_key_value_atomic_receipt_v1*out)const{return operations->collect(context,token,out);}
  int32_t cancel(uint64_t token)const{return operations->cancel(context,token);}
};
}
