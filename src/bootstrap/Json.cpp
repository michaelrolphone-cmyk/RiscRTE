#include "Json.h"
#include <cstdlib>
#include <chrono>
#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
namespace RiscBoot {
bool readJson(const char* filename, JsonDocument& doc, bool* closeRetained) {
  FILE* f=fopen(filename,"rb"); if (!f) return false;
  auto close=[&](){
    if(fclose(f)==0)return true;
    if(closeRetained)*closeRetained=true;
    return false;
  };
  constexpr size_t limit=65536;
  char* data=static_cast<char*>(malloc(limit+1));
  if (!data) { close(); return false; }
  size_t n=0; bool good=true;
  const auto start=std::chrono::steady_clock::now();
  while (n<=limit) {
    size_t got=fread(data+n,1,(limit+1-n)>512?512:limit+1-n,f); n+=got;
#ifdef ESP_PLATFORM
    vTaskDelay(1);
#endif
    if (std::chrono::steady_clock::now()-start>std::chrono::seconds(5)) { good=false; break; }
    if (!got) { good=feof(f) && !ferror(f); break; }
  }
  // Close exactly once, including failed/oversized/timed-out reads. Admission
  // must never succeed or hide retained ownership after a failed close.
  if(!close())good=false;
  good=good && n<=limit && parse(data,n,doc);
  free(data); return good;
}
}
