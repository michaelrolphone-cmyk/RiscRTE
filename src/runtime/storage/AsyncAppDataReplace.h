#pragma once
#include "StorageWorker.h"
#include <RiscAppDataV1.h>
namespace RiscStorage {
/* Native-owned mailbox plus caller-provided native payload allocation. Owner
 * methods serialize; workerStep is the sole worker entry. The executor and
 * context belong to firmware, never a loaded app/provider. */
class AsyncAppDataReplace {
 public:
  using Replace=int32_t(*)(void*,uint32_t,const char*,uint64_t,const void*,uint32_t);
  AsyncAppDataReplace(void*bytes,uint32_t capacity,Replace replace,void*context):bytes_(bytes),capacity_(capacity),replace_(replace),context_(context){worker_.bind(execute,this);}
  int32_t begin(uint32_t ns,const char*name,uint64_t expected,const void*bytes,uint32_t size,uint64_t*token){
   if(token)*token=0;
   if(!token||!ns||!name||!name[0]||std::strlen(name)>RISC_APP_DATA_NAME_MAX||(!bytes&&size)||size>capacity_||size>RISC_APP_DATA_FILE_MAX||!bytes_||!replace_)return RISC_APP_DATA_INVALID;
   if(!worker_.idle())return RISC_APP_DATA_BUSY;
   Request q{};q.nameSpace=ns;q.expected=expected;q.size=size;std::strcpy(q.name,name);
   if(size)std::memcpy(bytes_,bytes,size);
   return worker_.submit(&q,sizeof(q),token)==RISC_STORAGE_WORK_OK?RISC_APP_DATA_OK:RISC_APP_DATA_UNAVAILABLE;
  }
  int32_t collect(uint64_t token,int32_t*out){
   if(!out)return RISC_APP_DATA_INVALID;
   uint32_t size=0;int32_t status=0,result=0;
   int32_t rc=worker_.collect(token,&result,sizeof(result),&size,&status);
   if(rc==RISC_STORAGE_WORK_PENDING)return RISC_APP_DATA_BUSY;
   if(rc!=RISC_STORAGE_WORK_OK)return RISC_APP_DATA_CONTEXT;
   if(size!=sizeof(result)||status!=RISC_STORAGE_WORK_OK){*out=RISC_APP_DATA_RETAINED;return RISC_APP_DATA_OK;}
   *out=result;return RISC_APP_DATA_OK;
  }
  bool pending()const{return !worker_.idle();}
  bool workerStep(){return worker_.workerStep();}
 private:
  struct Request {uint32_t nameSpace,size;uint64_t expected;char name[RISC_APP_DATA_NAME_MAX+1];};
  static int32_t execute(void*context,const void*input,uint32_t n,void*output,uint32_t capacity,uint32_t*actual){
   auto*a=static_cast<AsyncAppDataReplace*>(context);
   if(n!=sizeof(Request)||capacity<sizeof(int32_t))return RISC_STORAGE_WORK_INVALID;
   const auto&q=*static_cast<const Request*>(input);
   const int32_t result=a->replace_(a->context_,q.nameSpace,q.name,q.expected,a->bytes_,q.size);
   std::memcpy(output,&result,sizeof(result));*actual=sizeof(result);return RISC_STORAGE_WORK_OK;
  }
  Worker worker_;void*bytes_;uint32_t capacity_;Replace replace_;void*context_;
};
}
