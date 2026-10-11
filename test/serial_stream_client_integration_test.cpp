#include "bootstrap/Runtime.h"
#include "runtime/streams/ProviderQueueHost.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
static std::string mode;
static unsigned phase=0;
static std::vector<std::string> events;
extern "C" const char* serial_witness_mode(){return mode.c_str();}
extern "C" unsigned serial_witness_phase(){return phase;}
extern "C" void serial_witness_set_phase(unsigned n){phase=n;}
extern "C" void serial_witness_event(const char* event){events.emplace_back(event);}
static unsigned count(const char* event){return unsigned(std::count(events.begin(),events.end(),event));}
static void write(const std::string& root,const char* name,const std::string& bytes){std::ofstream(root+"/"+name)<<bytes;}
int main(int argc,char** argv){
 assert(argc==4);const std::string root=argv[1];mode=argv[2];const std::string activation=argv[3];
 write(root,"board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
 write(root,"serial.json",R"({"type":"driver","id":"serial-witness","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"serial.elf","requires":[],"provides":[{"capability":"serial.port","api":1}]})");
 write(root,"app.json",R"({"type":"application","id":"portable-serial-witness","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"serial.port","api":1}]})");
 write(root,"boot.json",std::string(R"({"board":"board.json","default_app":"default.elf","provider_activation":")")+activation+R"(","drivers":[{"manifest":"serial.json"}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"serial.port","api":1,"instance_id":0}]}]})");
 static uint32_t now=0;
 auto* runtime=new RiscBoot::Runtime({[](){return true;},[](risc_runtime_health_v1* h){h->uptime_ms=++now;return true;},[](uint32_t){},[](const char*){return true;}});
 assert(runtime->prepare(root.c_str()));
 const bool retained=mode=="close-retained" || mode=="configure-rollback-retained";
 assert(runtime->run()==!retained && runtime->retained()==retained);
 for(const auto& event:events)assert(event.rfind("raw:",0)!=0);
 assert(count("adapter:open")==1 && count("adapter:close")==1);
 if(retained){
  assert(!count("client:fini") && !count("provider:quiesce") && !count("provider:stop"));
  assert(RuntimeStreams::Testing::allocatedBytes()==10);
  printf("portable serial %s/%s: exact custody retained; no raw fallback PASS\n",mode.c_str(),activation.c_str());fflush(stdout);std::_Exit(0);
 }
 assert(count("client:fini")==1 && count("provider:stop")==1 && !RuntimeStreams::Testing::allocatedBytes());
 delete runtime;
 printf("portable serial %s/%s: production client/broker/graph/provider/queue PASS\n",mode.c_str(),activation.c_str());
}
