#include "bootstrap/Runtime.h"
#include "diagnostics/Checkpoint.h"
#include <cassert>
#include <fstream>
#include <string>
using namespace RiscBoot;
namespace {
bool owner=true;std::string mode;unsigned writes=0,stages=0;
risc_diagnostic_checkpoint_client_v1 saved{},client{};
RiscDiagnostics::Checkpoint record{};Runtime* volatile held=nullptr;
bool owns(){return owner;}bool health(risc_runtime_health_v1*){return true;}
void delay(uint32_t){}bool log(const char*){return true;}
int32_t copy(const char* name,uint64_t token,const char* text,uint32_t size){++writes;return RiscDiagnostics::captureCheckpoint(record,1,2,name,token,text,size);}
void denied(const risc_diagnostic_checkpoint_client_v1& c){if(c.write)assert(c.write(c.invocation,"stale",5)==RISC_DIAGNOSTIC_CHECKPOINT_DENIED);}
}
extern "C" int checkpoint_test_stage(unsigned stage){
 ++stages;const auto* api=risc_runtime_get_api(1);assert(api);denied(saved);
 client={};client.struct_size=sizeof(client);
 const bool available=risc_runtime_diagnostic_checkpoint_client(api,&client);
 if(mode=="off"){assert(!available);return 0;}
 assert(available&&client.invocation&&client.api_version==1);
 owner=false;denied(client);owner=true;
 auto bad=client;bad.invocation^=UINT64_C(0x8000000000000000);denied(bad);
 assert(client.write(client.invocation,nullptr,1)==RISC_DIAGNOSTIC_CHECKPOINT_INVALID);
 const char text[]="copied checkpoint";assert(client.write(client.invocation,text,sizeof(text)-1)==0);
 assert(std::string(record.application)=="default.elf"&&std::string(record.text)==text);
 if(stage==1&&mode=="retained"){
  assert(api->retain_invocation());denied(client);return 0;
 }
 return 0;
}
int main(int argc,char** argv){
 assert(argc==3);const std::string root=argv[1];mode=argv[2];
 std::ofstream(root+"/board.json")<<R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"checkpoint-test","revision":"unspecified","buses":[],"devices":[]})";
 std::ofstream(root+"/boot.json")<<R"({"board":"board.json","default_app":"default.elf","drivers":[],"app_capabilities":[]})";
 for(unsigned i=0;i<(mode=="retained"?1u:3u);++i){
  Port port{owns,health,delay,log};if(mode!="off")port.diagnosticCheckpoint=copy;
  auto* r=new Runtime(port);assert(r->prepare(root.c_str()));const bool result=r->run();
  assert(result==(mode!="retained"));denied(client);denied(saved);saved=client;
  if(mode=="retained"){assert(r->retained());held=r;}else delete r;
 }
 assert(mode=="off"?writes==0:writes==stages);
 std::printf("Actual Runtime checkpoint %s stages=%u writes=%u PASS\n",mode.c_str(),stages,writes);
}
