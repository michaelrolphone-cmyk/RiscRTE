#pragma once
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
static std::vector<std::string> stageLines;
[[maybe_unused]] static bool stageHas(const char* text){for(const auto& line:stageLines)if(line.find(text)!=std::string::npos)return true;return false;}
[[maybe_unused]] static size_t stageCount(const char* text){size_t n=0;for(const auto& line:stageLines)if(line.find(text)!=std::string::npos)++n;return n;}
#if RISC_STAGE_LOGS
namespace RiscDiagnostics {
void timestamped(const char* format,...){
#ifdef RADIO_STAGE_SAFE
  RADIO_STAGE_SAFE();
#endif
  char text[256];va_list args;va_start(args,format);const int size=vsnprintf(text,sizeof(text),format,args);va_end(args);
  assert(size>0 && size<int(sizeof(text)));
  stageLines.push_back("RTE_STAGE us="+std::to_string(esp_timer_get_time())+" "+text);
}
}
#endif
