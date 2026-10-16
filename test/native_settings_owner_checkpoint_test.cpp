#include "bootstrap/Runtime.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <string>
#include "RuntimeFileFixture.h"
using R=RiscBoot::Runtime;
static const risc_runtime_api_v1*savedApi=nullptr;
static R*runtime;static bool held=false,terminal=false,inCheckpoint=false;static unsigned checkpoints=0,entries=0,delays=0,rawDelays=0;
static std::string mode;
static bool owner(){return true;}
static bool storageSafe(){return !held&&!terminal;}
static void delay(uint32_t){assert(!held&&!inCheckpoint);++delays;}
static void rawDelay(uint32_t){assert(inCheckpoint||terminal);++rawDelays;}
static bool checkpoint(bool admitNew){
 assert(admitNew);
 ++checkpoints;assert(!held&&!inCheckpoint);inCheckpoint=held=true;
 // This same call runs before provider readiness/lease acquisition. Attempts
 // to reenter acquisition, release, dispatch or reset cannot run app work.
 assert(!runtime->active()&&!runtime->residentResetSafe());
 assert(!risc_runtime_get_api(1));
 if(savedApi){risc_runtime_capability_v1 refused{};refused.struct_size=sizeof refused;
  assert(!savedApi->acquire(RISC_KEY_VALUE_CAPABILITY,1,1,&refused)&&!refused.api);}
 runtime->yield(2);assert(!runtime->retained()); // raw scheduler only, no callback recursion
 assert(inCheckpoint&&held);
 if(mode=="retained"){terminal=true;inCheckpoint=false;return false;}
 held=inCheckpoint=false;return true;
}
extern "C" void test_checkpoint_app(){
 ++entries;const auto*api=risc_runtime_get_api(1);savedApi=api;assert(api&&runtime->active()&&!held&&!inCheckpoint);
 const auto before=checkpoints;api->yield_ms(2);assert(checkpoints==before&&delays); // no start in ordinary yield
 risc_runtime_capability_v1 grant{};grant.struct_size=sizeof grant;
 assert(api->acquire(RISC_KEY_VALUE_CAPABILITY,1,1,&grant));assert(checkpoints>before&&!held&&!runtime->retained());
 assert(api->release(&grant));
}
int main(int argc,char**argv){assert(argc==3);const std::string root=argv[1];mode=argv[2];
 std::ofstream(root+"/board.json")<<R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})";
 std::ofstream(root+"/app.json")<<R"({"type":"application","id":"checkpoint-test","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"storage.key-value","api":1}]})";
 std::ofstream(root+"/boot.json")<<R"({"board":"board.json","default_app":"default.elf","drivers":[],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"storage.key-value","api":1,"instance_id":1}]}]})";
 RiscBoot::KeyValueBackend kv{nullptr,[](void*,uint32_t,const char*,void*,uint32_t,uint32_t*){return RISC_KEY_VALUE_NOT_FOUND;},[](void*,uint32_t,const char*,const void*,uint32_t){return RISC_KEY_VALUE_IO;}};
 RiscBoot::Port port{owner,[](risc_runtime_health_v1*){return true;},delay,[](const char*){return true;}};
 port.keyValue=&kv;port.appExitSafe=storageSafe;port.providerStorageSafe=storageSafe;port.retainedDelay=rawDelay;port.nativeOwnerCheckpoint=checkpoint;
 // Readiness is false during owned work; the checkpoint must be outside it.
 port.providerServicesReady=storageSafe;port.providerServiceBegin=[](){assert(!held&&!inCheckpoint);return true;};port.providerServiceEnd=[](){assert(!held&&!inCheckpoint);};
 static RiscTest::FileSources rFiles0({root.c_str()});auto*r=new R(port);assert(rFiles0.bind(*r));runtime=r;if(!r->prepare(root.c_str())){std::fprintf(stderr,"prepare: %s\n",r->error());return 2;}const bool ran=r->run();
 if(mode=="retained"){assert(!ran&&r->retained()&&!entries&&held);std::fflush(stdout);std::_Exit(0);}
 assert(ran&&!r->retained()&&entries==1&&checkpoints>=2&&rawDelays==checkpoints);delete r;
 puts("actual Runtime checkpoint: scheduler-only drain, reentry refusal, no yield admission and resumed app acquisition PASS");
}
