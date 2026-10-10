#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace RiscDiagnostics {
constexpr uint32_t CheckpointTextMax=768, CheckpointApplicationMax=192;
// Ordinary RAM, separate from the unchanged RTC journal. No borrowed pointers.
struct Checkpoint {
  uint64_t invocation=0;
  uint32_t sequence=0,boot=0,ms=0,length=0,applicationLength=0;
  bool truncated=false;
  char application[CheckpointApplicationMax+1]{},text[CheckpointTextMax+1]{};
};
inline int32_t captureCheckpoint(Checkpoint& out,uint32_t boot,uint32_t ms,
                                const char* app,uint64_t token,const char* text,uint32_t length) {
  if(!boot || !token || !app || !text || !length)return -2;
  const size_t appLength=strnlen(app,CheckpointApplicationMax+1);
  if(!appLength || appLength>CheckpointApplicationMax || out.sequence==UINT32_MAX)return -2;
  const uint32_t sequence=out.sequence+1;
  out={};out.sequence=sequence;out.boot=boot;out.ms=ms;out.invocation=token;
  out.applicationLength=uint32_t(appLength);out.length=length>CheckpointTextMax?CheckpointTextMax:length;
  out.truncated=length>CheckpointTextMax;
  for(size_t i=0;i<appLength;++i){const unsigned char c=app[i];out.application[i]=c>=32 && c<=126?char(c):'?';}
  for(uint32_t i=0;i<out.length;++i){const unsigned char c=text[i];out.text[i]=c>=32 && c<=126?char(c):'?';}
  return out.truncated?1:0;
}
}
