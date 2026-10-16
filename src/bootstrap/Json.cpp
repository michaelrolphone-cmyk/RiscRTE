#include "Json.h"
#include "RuntimeFiles.h"
#include "diagnostics/Performance.h"
#include <cstdlib>
#include <chrono>
#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
namespace RiscBoot {
namespace {
template<class Read,class Close>
bool readDocument(JsonDocument& doc,Read read,Close close) {
  constexpr size_t limit=65536;
  char* data=static_cast<char*>(malloc(limit+1));
  if (!data) { close(); return false; }
  size_t n=0; bool good=true;
  const auto start=std::chrono::steady_clock::now();
#ifdef ESP_PLATFORM
  auto yieldedAt=start;
  size_t sinceYield=0;
#endif
  while (n<=limit) {
    if (std::chrono::steady_clock::now()-start>std::chrono::seconds(5)) { good=false; break; }
    size_t got=0;const bool readOk=read(data+n,(limit+1-n)>512?512:limit+1-n,got);n+=got;
#ifdef ESP_PLATFORM
    // Preserve bounded reads and the total deadline without forcing a scheduler
    // tick after every 512 bytes. The final remainder checkpoint also bounds
    // consecutive small metadata files; admission still reads/parses each one.
    sinceYield+=got;
    const auto now=std::chrono::steady_clock::now();
    if (sinceYield>=32*1024 || now-yieldedAt>=std::chrono::milliseconds(2) ||
        ((!readOk || !got || n>limit) && sinceYield)) {
      {RiscPerf::AggregateScope wait(33,1);vTaskDelay(1);}
      yieldedAt=std::chrono::steady_clock::now();sinceYield=0;
    }
#endif
    if (std::chrono::steady_clock::now()-start>std::chrono::seconds(5)) { good=false; break; }
    if (!readOk || !got) { good=readOk; break; }
  }
  // Close exactly once, including failed/oversized/timed-out reads. Admission
  // must never succeed or hide retained ownership after a failed close.
  if(!close())good=false;
  good=good && n<=limit && parse(data,n,doc);
  free(data); return good;
}
}
bool readJson(RuntimeFileView source,const char* filename,JsonDocument& doc,bool* closeRetained) {
  RiscPerf::Scope trace(28,29,RiscPerf::identity(filename));
  const auto retained=[&]{if(closeRetained && source.retained())*closeRetained=true;};
  uint32_t handle=0;
  if(source.open(filename,&handle)!=RISC_FILES_OK){retained();return false;}
  const bool good=readDocument(doc,[&](void* bytes,size_t count,size_t& got){
    uint32_t actual=0;const int32_t rc=source.read(handle,bytes,static_cast<uint32_t>(count),&actual);
    got=actual;return rc==RISC_FILES_OK;
  },[&]{const bool closed=source.close(handle)==RISC_FILES_OK;retained();return closed;});
  retained();return good && !source.retained();
}
}
