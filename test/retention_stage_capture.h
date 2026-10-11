#pragma once
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
static std::vector<std::string> lines;
#if RISC_STAGE_LOGS
namespace RiscDiagnostics {
uint64_t monotonicUs(){return 0;}
void timestamped(const char* format,...){
 char line[256];va_list args;va_start(args,format);const int n=vsnprintf(line,sizeof(line),format,args);va_end(args);
 assert(n>0 && n<int(sizeof(line)));lines.emplace_back(line);
}
}
#endif
[[maybe_unused]] static size_t count(const char* text){size_t n=0;for(const auto& line:lines)if(line.find(text)!=std::string::npos)++n;return n;}
