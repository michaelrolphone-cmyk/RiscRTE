#include "bootstrap/Runtime.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
static std::vector<std::string> lines;
static bool cleanupAllowed=false;
extern "C" bool test_startup_cleanup(){return cleanupAllowed;}
int main(int argc,char**argv){assert(argc==2);const std::string root=argv[1];
std::ofstream(root+"/board.json")<<R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})";
std::ofstream(root+"/driver.json")<<R"({"type":"driver","id":"startup-failure","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"driver.elf","requires":[],"provides":[{"capability":"test.startup","api":1}]})";
std::ofstream(root+"/boot.json")<<R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"driver.json"}]})";
RiscBoot::Runtime r({[](){return true;},[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*s){lines.emplace_back(s);return true;}});
assert(r.prepare(root.c_str()));assert(!r.run() && r.retained());assert(strstr(r.error(),"primary probe pullup denied"));
bool primary=false,cleanup=false,begin=false;for(const auto&s:lines){primary|=s.find("primary probe pullup denied")!=std::string::npos;cleanup|=s.find("RTE_CLEANUP")!=std::string::npos;begin|=s.find("phase=start")!=std::string::npos;}
assert(primary&&cleanup&&begin);cleanupAllowed=true;puts("Startup failure: primary provider diagnostics survive failed quiescence/shutdown and bounded stage logs PASS");}
