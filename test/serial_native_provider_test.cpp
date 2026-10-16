#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main storage_files_adapter_fixture_main
#include "storage_files_adapter_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include "bootstrap/Runtime.h"
#include "runtime/serial/Files.h"
#include "RuntimeFileFixture.h"
#include <fstream>
#include <unistd.h>
static Backend volume;
static GenericProvider generic;
static bool direct,owned=true,platformSafe=true,failRelease=false;
static std::string root,mode;
static RiscBoot::Runtime* runtime;
static RiscBoot::RuntimeFiles client;
static RiscSerial::Files endpoint;
static const risc_runtime_api_v1* app;
static unsigned sequence=0,serialDispatches=0,quiesces=0,refreshes=0;
static bool owner(){return owned;}
static bool safe(){return platformSafe;}
static bool health(risc_runtime_health_v1*){return true;}
static bool logLine(const char*){return true;}
static void delay(uint32_t){}
extern "C" const void* storage_runtime_backend(unsigned i){assert(i==0);return direct?static_cast<const void*>(&generic.api):static_cast<const void*>(volume.base());}
extern "C" bool storage_runtime_backend_safe(unsigned i){assert(i==0);++quiesces;return !failRelease&&(direct?generic.handles.empty():volume.handles.empty());}
extern "C" const char* storage_runtime_capability(){return direct?"storage.files":"storage.volume";}
static bool checkpoint(){assert(!runtime->active()&&!risc_runtime_get_api(1));++serialDispatches;endpoint.checkpoint();return endpoint.custodySafe();}
static std::string command(const char* text,int expected=0){
 const std::string line="RTEF1 12345678 "+std::to_string(++sequence)+" "+text+"\n";
 for(unsigned char b:line)endpoint.input(b);
 const unsigned n=serialDispatches;app->yield_ms(1);assert(serialDispatches==n);
 const auto rc=app->service_checkpoint();assert(rc==(expected==RISC_FILES_RETAINED?RISC_RUNTIME_CHECKPOINT_RETAINED:RISC_RUNTIME_CHECKPOINT_OK));
 std::string out;while(endpoint.outputSize()){out.append(endpoint.output(),endpoint.outputSize());endpoint.consumeOutput(endpoint.outputSize());}
 const std::string prefix="RTEF1 12345678 "+std::to_string(sequence)+" "+std::to_string(expected);
 if(out.rfind(prefix,0)!=0)fprintf(stderr,"command %s -> %s expected %s\n",text,out.c_str(),prefix.c_str());
 assert(out.rfind(prefix,0)==0);return out;
}
extern "C" void test_atomic_app(unsigned hook,unsigned){
 app=risc_runtime_get_api(1);assert(app);
 if(hook!=1){assert(app->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_CONTEXT);return;}
 assert(!runtime->bindNativeFiles("runtime-files-first",0,client,owner,safe));
 risc_runtime_capability_v1 denied{sizeof(denied)};assert(!app->acquire("storage.files",1,12,&denied));
 command("HELLO");command("PATH 0 2f66696c65");
 if(mode=="absent"||mode=="wrong-instance"||mode=="refresh-absent"||mode=="refresh-refused"){command("GET 12",RISC_FILES_UNAVAILABLE);return;}
 command("PUT 12 replace 2");command("WRITE 0 7879");
 if(mode=="failed-close"){
  if(direct)generic.results["close"]=RISC_FILES_IO;else volume.fail="file_close";
  command("ABORT",RISC_FILES_RETAINED);return;
 }
 if(mode=="disconnect"){endpoint.disconnect();assert(app->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_OK);return;}
 command("COMMIT 8fe62899");command("PATH 0 2f66696c65");
 command("GET 12");assert(command("READ 0").find("7879")!=std::string::npos);command("CLOSE");
 assert((direct?generic.files["/file"].bytes:volume.nodes["/file"].data)=="xy");
 if(mode=="failed-release")failRelease=true;
}
static void file(const char* path,const std::string& value){std::ofstream(root+"/"+path)<<value;}
int main(int argc,char**argv){
 assert(argc==3);root=argv[1];mode=argv[2];direct=mode!="volume"&&mode.rfind("refresh-",0)!=0;
 const bool prepare=mode.rfind("refresh-",0)==0;
 volume.base()->refresh=[](void*){++refreshes;assert(runtime&&!runtime->active());if(mode=="refresh-refused")return false;if(mode=="refresh-fail"){volume.state=RISC_STORAGE_STATE_RETAINED;return false;}if(mode=="refresh-poison"){volume.state=RISC_STORAGE_STATE_RETAINED;return true;}volume.ready=mode!="refresh-absent";volume.state=volume.ready?RISC_STORAGE_STATE_READY:RISC_STORAGE_STATE_UNAVAILABLE;return true;};
 file("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
 file("provider.json",std::string(R"({"type":"driver","id":"runtime-files-first","version":"1.0.0","architecture":"xtensa-esp32s3","driver_abi":2,"file_name":"provider.elf","requires":[],"provides":[{"capability":")")+(direct?"storage.files":"storage.volume")+R"(","api":1}]})");
 file("app.json",R"({"type":"application","id":"native-files-test","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[]})");
 file("boot.json",R"({"board":"board.json","default_app":"default.elf","provider_activation":"demand","drivers":[{"manifest":"provider.json"}],"app_capabilities":[{"manifest":"app.json","grants":[]}]})");
 if(prepare && mode!="refresh-already-ready"){volume.ready=false;volume.state=RISC_STORAGE_STATE_UNAVAILABLE;}
 RiscBoot::Port port{owner,health,delay,logLine};port.appExitSafe=safe;port.providerStorageSafe=safe;
 port.serialFilesCheckpoint=checkpoint;port.serialFilesShutdown=[](){return endpoint.shutdown();};
 RiscTest::FileSources sources({root.c_str()});runtime=new RiscBoot::Runtime(port);assert(sources.bind(*runtime));
 assert(!runtime->bindNativeFiles("runtime-files-first",0,client,owner,safe));assert(runtime->prepare(root.c_str()));
 const char* id=mode=="absent"?"missing":"runtime-files-first";const uint64_t instance=mode=="wrong-instance"?44:0;
 owned=false;assert(!runtime->bindNativeFiles(id,instance,client,owner,safe));owned=true;
 assert(runtime->bindNativeFiles(id,instance,client,owner,safe,prepare)==(mode!="absent"&&mode!="wrong-instance"));
 assert(!client.bound());
 RiscSerial::Namespace map[]={{12,"Native files",&client,true}};assert(endpoint.bind(map,1,{owner,safe,[]()->uint32_t{return 0;}}));
 const bool retained=mode=="failed-close"||mode=="failed-release"||mode=="refresh-fail"||mode=="refresh-poison";
 assert(runtime->run()!=retained);assert(runtime->retained()==retained);assert(refreshes==((prepare&&mode!="refresh-already-ready")?1u:0u));
 if(retained){assert(client.retained());puts("Native graph serial retained custody PASS");fflush(stdout);_exit(0);}
 assert(!client.bound());uint32_t h=0;assert(client.open("/file",RISC_FILES_READ,&h)==RISC_FILES_UNAVAILABLE);
 assert((mode=="absent"||mode=="wrong-instance")?quiesces==0:quiesces==1);
 delete runtime;puts("Native graph serial explicit provider admission/wire/retirement PASS");
}
