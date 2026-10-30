#pragma once
#include <RiscPsramCacheV1.h>
namespace RiscMemory {
struct CacheBackend {
 bool (*task)();
 bool (*supported)();
 bool (*range)(uintptr_t,size_t);
 int64_t (*nowUs)();
 bool (*writeback)(uintptr_t,size_t);
 void (*yield)();
 uintptr_t low,high; // Exclusive end of PSRAM data virtual-address window.
};
// Stateless: distinct stable buffers may be written concurrently. The native
// SDK supplies its own shared serialization; do not add an unrelated lock that
// falsely purports to serialize SDK callers or flash operations.
inline int32_t writeback(const CacheBackend& b,uintptr_t address,size_t bytes){
 if(!b.task())return RISC_PSRAM_CACHE_CONTEXT;
 if(!b.supported())return RISC_PSRAM_CACHE_UNSUPPORTED;
 if(!bytes || bytes>1024u*1024u || address<b.low || address>=b.high ||
    bytes>b.high-address || !b.range(address,bytes))return RISC_PSRAM_CACHE_INVALID;
 const int64_t start=b.nowUs();if(start<0)return RISC_PSRAM_CACHE_DEADLINE;
 int64_t previous=start,checkpoint=start;size_t offset=0,sinceYield=0;
 auto expired=[&](int64_t now){
   const bool bad=now<previous || now-start>=100000;
   previous=now;return bad;
 };
 while(offset<bytes){
   if(expired(b.nowUs()))return RISC_PSRAM_CACHE_DEADLINE;
   const size_t left=bytes-offset,chunk=left>4096?4096:left;
   if(!b.writeback(address+offset,chunk))return RISC_PSRAM_CACHE_IO;
   offset+=chunk;sinceYield+=chunk;
   const int64_t now=b.nowUs();if(expired(now))return RISC_PSRAM_CACHE_DEADLINE;
   if(offset<bytes && (sinceYield>=32768 || now-checkpoint>=2000)){
     b.yield();checkpoint=b.nowUs();if(expired(checkpoint))return RISC_PSRAM_CACHE_DEADLINE;
     sinceYield=0;
   }
 }
 return RISC_PSRAM_CACHE_OK;
}
}
