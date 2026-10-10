#include "bootstrap/Runtime.h"
#include "runtime/diagnostics/FailureEvidenceStore.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
using namespace RiscBoot;
namespace {
bool owner=true,safe=true,held=false;
std::string root,mode;
Runtime* volatile retainedRuntime=nullptr;
const risc_runtime_api_v1* api;
risc_failure_evidence_client_v1 saved{},client{};
risc_failure_evidence_v1 record{};
RiscFailureEvidence::Image image{};
RiscFailureEvidence::State evidenceState{};
unsigned visits=0,acks=0,reads=0,retentions=0,logs=0,lastLogs=0;
std::vector<uint32_t> phases;
std::vector<std::string> names;
bool owned(){return owner;}
bool healthy(risc_runtime_health_v1*){assert(!held);return true;}
bool custody(){assert(!held);return safe;}
void delay(uint32_t){assert(!held);}
bool log(const char*){assert(!held);++logs;return true;}
int32_t read(risc_failure_evidence_v1* out){++reads;return RiscFailureEvidence::read(image,evidenceState,*out);}
int32_t ack(uint32_t boot,uint32_t seq){assert(!held);const int32_t result=RiscFailureEvidence::acknowledge(image,evidenceState,boot,seq);if(result==0)++acks;return result;}
void breadcrumb(const char* app,uint64_t invocation,uint32_t phase,uint32_t role){assert(!held);assert(invocation || phase==RISC_FAILURE_PHASE_IDLE);phases.push_back(phase);names.emplace_back(app);assert(RiscFailureEvidence::breadcrumb(image,evidenceState,app,invocation,phase,role));}
void retain(int32_t status,const char* detail){assert(status==RISC_RESIDENT_RETAINED);if(!held){assert(!RiscFailureEvidence::captureRetention(image,evidenceState,status,detail));held=true;lastLogs=logs;++retentions;}else assert(!RiscFailureEvidence::captureRetention(image,evidenceState,status,detail));}
FailureEvidenceBackend backend{read,ack,breadcrumb,retain};
void setup(){
 std::ofstream(root+"/board.json")<<R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})";
 JsonDocument boot;boot["board"]="board.json";boot["default_app"]="host.elf";boot["drivers"].to<JsonArray>();
 auto shell=boot["resident_shell"].to<JsonObject>();shell["api"]=1;shell["host"]="host.elf";shell["foreground"].to<JsonArray>().add("child.elf");shell["legacy"].to<JsonArray>().add("legacy.elf");
 auto policies=boot["app_capabilities"].to<JsonArray>();
 for(const char* name:{"host","child","legacy"}){
  JsonDocument app;app["type"]="application";app["id"]=name;app["version"]="1.0.0";app["architecture"]="xtensa-esp32s3";app["file_name"]=std::string(name)+".elf";app["entry"]="app_main";app["requires"].to<JsonArray>();
  std::string bytes;serializeJson(app,bytes);std::ofstream(root+"/"+name+".json")<<bytes;
  auto policy=policies.add<JsonObject>();policy["manifest"]=std::string(name)+".json";policy["grants"].to<JsonArray>();
 }
 std::string bytes;serializeJson(boot,bytes);std::ofstream(root+"/boot.json")<<bytes;
 const uint8_t hash[32]={1,2,3};RiscFailureEvidence::State crashed;
 assert(RiscFailureEvidence::boot(image,crashed,RiscFailureEvidence::Reset::PowerOn,1,hash));
 assert(RiscFailureEvidence::breadcrumb(image,crashed,"previous.elf",77,RISC_FAILURE_PHASE_MAIN,2));
 RiscFailureEvidence::Registers regs{};regs.flags=RISC_FAILURE_REGISTERS;regs.pc=0x42000020;regs.sp=0x3fc90000;
 assert(RiscFailureEvidence::capturePanicBase(image,crashed,regs));
 assert(RiscFailureEvidence::boot(image,evidenceState,RiscFailureEvidence::Reset::Panic,4,hash));
 assert(RiscFailureEvidence::read(image,evidenceState,record)==0);
}
bool get(risc_failure_evidence_client_v1& out){out={};out.struct_size=sizeof(out);return api->failure_evidence(&out);}
void denied(const risc_failure_evidence_client_v1& c){if(!c.read)return;risc_failure_evidence_v1 out{};out.struct_size=sizeof(out);assert(c.read(c.invocation,&out)==RISC_FAILURE_EVIDENCE_DENIED);assert(c.acknowledge(c.invocation,record.record_boot,record.record_sequence)==RISC_FAILURE_EVIDENCE_DENIED);}
int32_t dispatch(void*,const risc_resident_request_v1*,risc_resident_reply_v1*){return RISC_RESIDENT_OK;}
}
extern "C" int failure_test_init(unsigned){api=risc_runtime_get_api(1);assert(api && api->struct_size>=RISC_RUNTIME_FAILURE_EVIDENCE_V1_SIZE);risc_failure_evidence_client_v1 out;assert(!get(out));denied(saved);return 0;}
extern "C" void failure_test_fini(unsigned){risc_failure_evidence_client_v1 out;assert(!get(out));denied(client);}
extern "C" void failure_test_main(unsigned role){
 risc_failure_evidence_client_v1 out;
 if(role){assert(!get(out));denied(saved);denied(client);return;}
 ++visits;
 if(mode=="off"){assert(!get(out));return;}
 assert(get(client));
 if(saved.read)denied(saved);
 risc_failure_evidence_v1 a{},b{};a.struct_size=b.struct_size=sizeof(a);
 assert(client.read(client.invocation,&a)==0);assert(client.read(client.invocation,&b)==0);assert(!std::memcmp(&a,&b,sizeof(a)));
 owner=false;denied(client);assert(!get(out));owner=true;
 auto malformed=client;malformed.invocation^=UINT64_C(0x8000000000000000);denied(malformed);
 assert(client.read(client.invocation,nullptr)==RISC_FAILURE_EVIDENCE_INVALID);
 if(mode=="retained"){
  assert(api->retain_invocation());assert(held);denied(saved);
  a.struct_size=sizeof(a);assert(client.read(client.invocation,&a)==0 && a.kind==RISC_FAILURE_NATIVE_PANIC);
  assert(a.record_boot==record.record_boot && a.record_sequence==record.record_sequence && (a.flags&RISC_FAILURE_PENDING));
  assert(client.acknowledge(client.invocation,record.record_boot,record.record_sequence)==RISC_FAILURE_EVIDENCE_DENIED);
  assert(!get(out));assert(api->retain_invocation());assert(retentions==1);return;
 }
 if(mode=="custody"){
  safe=false;assert(!get(out));assert(client.acknowledge(client.invocation,record.record_boot,record.record_sequence)==RISC_FAILURE_EVIDENCE_DENIED);safe=true;return;
 }
 if(visits<3)assert(a.flags&RISC_FAILURE_PENDING);else assert(!(a.flags&RISC_FAILURE_PENDING));
 assert(client.acknowledge(client.invocation,record.record_boot,record.record_sequence+1)==RISC_FAILURE_EVIDENCE_STALE);
 if(visits==2){assert(client.acknowledge(client.invocation,record.record_boot,record.record_sequence)==0);assert(client.acknowledge(client.invocation,record.record_boot,record.record_sequence)==0);}
 risc_resident_client_v1 resident{};resident.struct_size=sizeof(resident);assert(api->resident_shell(&resident));
 risc_resident_callbacks_v1 callbacks{1,sizeof(callbacks),nullptr,dispatch,nullptr};assert(resident.register_shell(resident.invocation,&callbacks)==0);
 risc_resident_result_v1 result{};result.struct_size=sizeof(result);
 assert(resident.run_foreground(resident.invocation,"child.elf",&result)==0);
 if(visits<3){
  assert(resident.run_foreground(resident.invocation,"legacy.elf",&result)==RISC_RESIDENT_HANDOFF);
  assert(client.acknowledge(client.invocation,record.record_boot,record.record_sequence)==RISC_FAILURE_EVIDENCE_DENIED);
  saved=client;return; // first presentation failed: deliberately no ack
 }
}
int main(int argc,char** argv){
 assert(argc==3);root=argv[1];mode=argv[2];setup();
 Port port{owned,healthy,delay,log};port.appExitSafe=custody;if(mode!="off")port.failureEvidence=&backend;
 auto* runtime=new Runtime(port);assert(runtime->prepare(root.c_str()));const bool terminal=mode=="retained";
 assert(runtime->run()!=terminal);assert(runtime->retained()==terminal);denied(client);denied(saved);
 if(terminal){assert(retentions==1 && logs==lastLogs);retainedRuntime=runtime;}
 else {delete runtime;if(mode=="normal")assert(visits==3 && acks==2 && reads==6);}
 if(mode=="off")assert(phases.empty() && !reads && !acks && !retentions);
 else {assert(!phases.empty() && phases.front()==RISC_FAILURE_PHASE_LOAD);assert(names.front()=="host.elf");}
 std::printf("Failure evidence lifecycle %s PASS\n",mode.c_str());
}
