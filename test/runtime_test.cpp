#include "bootstrap/Runtime.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
using namespace RiscBoot;
static unsigned generation=0, beats=0;
static bool heartbeatMode=false;
static std::vector<std::string> lines;
static bool owner(){return true;}
static bool health(risc_runtime_health_v1* h){if(heartbeatMode){ if(beats==3)return false; h->uptime_ms=(++generation)*2000; h->free_heap=123456; h->app_address=0x10000; snprintf(h->target,sizeof(h->target),"host-test"); return true;} h->uptime_ms=++generation;return true;}
static void delay(uint32_t){}
static bool logLine(const char* s){lines.emplace_back(s);if(heartbeatMode)++beats;return true;}
static void write(const std::string& p,const std::string& s){std::ofstream(p)<<s;}
static const char* board=R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[{"instance_id":7,"chip":{"vendor":"test","model":"gpio","revision":"unspecified"},"compatible":"test,gpio","config_type":"gpio.bank","config_version":1,"config":{"pins":[5],"active_high":true,"pull_up":false,"debounce_us":0,"long_press_us":0,"click_min_us":0}}]})";
static const char* manifest=R"({"type":"driver","id":"probe","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"probe.elf","requires":[{"capability":"hardware.device","api":1}],"provides":[{"capability":"test.probe","api":1}],"hardware_compatibility":[{"compatible":"test,gpio","revisions":["unspecified"],"config_type":"gpio.bank","config_version":1}]})";
int main(int argc,char** argv){
  assert(argc==2);std::string root=argv[1];
  write(root+"/board.json",board);write(root+"/probe.json",manifest);
  write(root+"/boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"probe.json","instance_id":7}]})");
  {
    Runtime runtime({owner,health,delay,logLine});
    assert(!risc_runtime_get_api(1));
    assert(runtime.prepare(root.c_str()));
    assert(runtime.run());assert(!risc_runtime_get_api(1));
    assert((lines==std::vector<std::string>{"TEST default","TEST child","TEST default","RTE_APP child=failed action=reload-default","TEST default"}));
    assert(generation==3);
  }
  // Invalid board always rejects before any driver entry/load or default app.
  JsonDocument doc;assert(parse(board,strlen(board),doc));
  doc["devices"][0]["config"]["pins"][0]=49;std::string bad;serializeJson(doc,bad);write(root+"/board.json",bad);
  {Runtime runtime({owner,health,delay,logLine});assert(!runtime.prepare(root.c_str()));assert(!runtime.run());}
  write(root+"/board.json",board);
  assert(parse(manifest,strlen(manifest),doc));doc["hardware_compatibility"][0]["compatible"]="wrong,chip";bad.clear();serializeJson(doc,bad);write(root+"/probe.json",bad);
  {Runtime runtime({owner,health,delay,logLine});assert(!runtime.prepare(root.c_str()));}
  write(root+"/probe.json",manifest);
  write(root+"/boot.json",R"({"board":"board.json","default_app":"../default.elf","drivers":[]})");
  {Runtime runtime({owner,health,delay,logLine});assert(!runtime.prepare(root.c_str()));}
  // Missing default fails once; never searches/reboots/retries recursively.
  write(root+"/boot.json",R"({"board":"board.json","default_app":"absent.elf","drivers":[]})");
  {Runtime runtime({owner,health,delay,logLine});assert(runtime.prepare(root.c_str()));assert(!runtime.run());}
  assert(!parse("{\"id\":1,\"id\":2}",15,doc));
  assert(parse(board,strlen(board),doc));
  doc["devices"].as<JsonArray>().add(doc["devices"][0]);
  {Board b;assert(!b.load(doc.as<JsonObjectConst>()));}
  assert(parse(board,strlen(board),doc));
  {Board b;assert(b.reservePin(5));assert(!b.load(doc.as<JsonObjectConst>()));}
  write(root+"/boot.json",R"({"board":"board.json","default_app":"heartbeat.elf","drivers":[]})");
  heartbeatMode=true;generation=0;lines.clear();
  {Runtime runtime({owner,health,delay,logLine});assert(runtime.prepare(root.c_str()));assert(runtime.run());}
  assert(beats==3 && lines.size()==3);
  for(size_t i=0;i<3;++i) {
    unsigned seq=0,uptime=0,heap=0,app=0;
    assert(sscanf(lines[i].c_str(),"RTE_HEARTBEAT version=1.0.0 target=host-test mac=00:00:00:00:00:00 sequence=%u uptime_ms=%u heap=%u app=0x%x",&seq,&uptime,&heap,&app)==4);
    assert(seq==i+1 && uptime==(i+1)*2000 && heap>0 && app==0x10000);
  }
  puts("Heartbeat default app: three exact-format monotonic health lines PASS");
  puts("Runtime integration: mapped provider, default/child/default/missing/default, manifest/path rejection PASS");
}
