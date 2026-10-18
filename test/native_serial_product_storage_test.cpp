#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main storage_files_adapter_fixture_main
#include "storage_files_adapter_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include "ports/esp32s3/NativeSerialStorageBinding.h"
#include "RuntimeFileFixture.h"
#include <fstream>
#include <unistd.h>
namespace S=RiscCpu::NativeSerialStorage;
namespace N=RiscCpu::NativeSerialFiles;
static Backend backend;
static RiscBoot::Runtime* runtime;
static const risc_runtime_api_v1* app;
static std::string root,mode;
static unsigned starts,stops,quiesces,refreshes,sequence,appCalls;
static bool owned=true,allowed=true;
static bool owner(){return owned;}
static bool safe(){return allowed;}
extern "C" int64_t esp_timer_get_time(){return 0;}
extern "C" bool xPortInIsrContext(){return false;}
extern "C" const void* serial_product_backend(){return backend.base();}
extern "C" void serial_product_started(){++starts;}
extern "C" bool serial_product_quiesce(){++quiesces;return mode!="failed-release" && backend.handles.empty();}
extern "C" void serial_product_stopped(){++stops;}
static bool checkpoint(){assert(!runtime->active()&&!risc_runtime_get_api(1));return N::checkpoint();}
static std::string command(const char* text,int expected=0){
  const std::string line="RTEF1 12345678 "+std::to_string(++sequence)+" "+text+"\n";
  const auto calls=backend.totalCalls();for(unsigned char byte:line)N::input(byte);
  app->yield_ms(1);assert(backend.totalCalls()==calls);
  assert(app->service_checkpoint()==(expected==RISC_FILES_RETAINED?RISC_RUNTIME_CHECKPOINT_RETAINED:RISC_RUNTIME_CHECKPOINT_OK));
  std::string out;while(N::outputSize()){out.append(N::output(),N::outputSize());N::consumeOutput(N::outputSize());}
  const auto prefix="RTEF1 12345678 "+std::to_string(sequence)+" "+std::to_string(expected)+" ";
  if(out.rfind(prefix,0))fprintf(stderr,"%s -> %s expected %s\n",text,out.c_str(),prefix.c_str());
  assert(out.rfind(prefix,0)==0);return out;
}
extern "C" void test_atomic_app(unsigned hook,unsigned){
  ++appCalls;app=risc_runtime_get_api(1);assert(app);
  if(hook!=1){assert(app->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_CONTEXT);return;}
  assert(starts==1 && S::sdFiles().bound() && !S::bindSd(*runtime,owner,safe));
  risc_runtime_capability_v1 denied{sizeof(denied)};
  assert(!app->acquire("storage.volume",1,TEST_PRODUCT_INSTANCE,&denied));
  assert(!app->acquire("storage.files",1,TEST_PRODUCT_INSTANCE,&denied));
  command("HELLO");assert(command("NS 0").find(" NS 12 1 53442063617264\n")!=std::string::npos);
  command("PATH 0 2f66696c65");
  if(mode=="absent-media"||mode=="refresh-refused"){
    const auto calls=backend.totalCalls();command("GET 12",RISC_FILES_UNAVAILABLE);
    command("PUT 12 replace 2",RISC_FILES_UNAVAILABLE);
    assert(backend.totalCalls()>=calls && backend.count("file_open")==0 && backend.count("file_open_flags")==0);
    assert(N::custodySafe() && backend.handles.empty() && backend.nodes["/file"].data=="abcdef");return;
  }
  command("PUT 12 replace 2");command("WRITE 0 7879");
  if(mode=="failed-close"){
    backend.fail="file_close";command("ABORT",RISC_FILES_RETAINED);
    assert(runtime->retained() && S::sdFiles().retained() && !N::custodySafe());
    const auto calls=backend.totalCalls();N::disconnect();assert(!N::checkpoint()&&!N::shutdown());
    assert(backend.totalCalls()==calls);return;
  }
  command("COMMIT 8fe62899");command("PATH 0 2f66696c65");command("GET 12");
  assert(command("READ 0").find(" READ 2 7879\n")!=std::string::npos);command("CLOSE");
  assert(backend.nodes["/file"].data=="xy" && backend.nodes["/dir/a"].data=="abc");
}
static void file(const char* path,const std::string& contents){std::ofstream(root+"/"+path)<<contents;}
int main(int argc,char** argv){
  assert(argc==3);root=argv[1];mode=argv[2];
  static_assert(TEST_PRODUCT_INSTANCE==RISC_NATIVE_SERIAL_SD_INSTANCE,"test selects exact product instance");
  assert(!strcmp(S::Detail::sdProvider,TEST_PRODUCT_PROVIDER));
  const unsigned instance=mode=="wrong-instance"?TEST_PRODUCT_INSTANCE+1:TEST_PRODUCT_INSTANCE;
  const std::string id=mode=="wrong-provider"?"unselected-provider":TEST_PRODUCT_PROVIDER;
  const std::string capability=mode=="wrong-capability"?"storage.files":"storage.volume";
  const unsigned api=mode=="wrong-api"?2:1;
  file("board.json",std::string(R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"deliberately-unrelated-product","revision":"unspecified","buses":[],"devices":[{"instance_id":)")+std::to_string(mode=="invalid-board"?0:instance)+R"(,"chip":{"vendor":"test","model":"gpio","revision":"unspecified"},"compatible":"test,gpio","config_type":"gpio.bank","config_version":1,"config":{"pins":[5],"active_high":true,"pull_up":false,"debounce_us":0,"long_press_us":0,"click_min_us":0}}]})");
  file("provider.json",std::string(R"({"type":"driver","id":")")+id+R"(","version":"1.0.0","architecture":"xtensa-esp32s3","driver_abi":2,"file_name":"provider.elf","requires":[{"capability":"hardware.device","api":1}],"provides":[{"capability":")"+capability+R"(","api":)"+std::to_string(api)+R"(}],"hardware_compatibility":[{"compatible":"test,gpio","revisions":["unspecified"],"config_type":"gpio.bank","config_version":1}]})");
  file("app.json",R"({"type":"application","id":"serial-product-test","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[]})");
  const std::string driver=R"({"manifest":"provider.json","instance_id":)"+std::to_string(instance)+"}";
  file("boot.json",std::string(R"({"board":"board.json","default_app":"default.elf","provider_activation":"demand","drivers":[)")+(mode=="unadmitted"?"":driver)+(mode=="ambiguous"?","+driver:"")+R"(],"app_capabilities":[{"manifest":"app.json","grants":[]}]})");
  backend.ready=false;backend.state=RISC_STORAGE_STATE_UNAVAILABLE;
  backend.base()->refresh=[](void*){
    ++refreshes;assert(runtime&&!runtime->active());
    if(mode=="refresh-retained"){backend.state=RISC_STORAGE_STATE_RETAINED;return false;}
    if(mode=="refresh-refused")return false;
    backend.ready=mode!="absent-media";backend.state=backend.ready?RISC_STORAGE_STATE_READY:RISC_STORAGE_STATE_UNAVAILABLE;return true;
  };
  RiscBoot::Port port{owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;}};
  port.appExitSafe=safe;port.providerStorageSafe=safe;port.serialFilesCheckpoint=checkpoint;port.serialFilesShutdown=N::shutdown;port.serialFilesStop=N::stop;
  RiscTest::FileSources sources({root.c_str()});runtime=new RiscBoot::Runtime(port);assert(sources.bind(*runtime));
  assert(!S::bindSd(*runtime,owner,safe));assert(S::prepare(owner,safe));
  assert(S::admission()->count==1 && S::admission()->namespaces[0].id==12);
  assert(S::admission()->namespaces[0].files==&S::sdFiles());assert(!S::bindSd(*runtime,owner,safe));
  const bool prepared=runtime->prepare(root.c_str());
  const bool invalid=mode=="ambiguous"||mode=="invalid-board";
  if(prepared==invalid)fprintf(stderr,"prepare=%d mode=%s error=%s\n",prepared,mode.c_str(),runtime->error());
  assert(prepared!=invalid);
  owned=false;assert(!S::bindSd(*runtime,owner,safe));owned=true;
  allowed=false;assert(!S::bindSd(*runtime,owner,safe));allowed=true;
  const bool rejected=invalid||mode=="unadmitted"||mode=="wrong-provider"||mode=="wrong-instance"||mode=="wrong-api"||mode=="wrong-capability";
  assert(S::bindSd(*runtime,owner,safe)!=rejected);
  assert(!starts && !refreshes && !backend.totalCalls() && !S::sdFiles().bound());
  if(rejected){delete runtime;puts("Product startup exact selection rejected before activation PASS");return 0;}
  assert(!S::bindSd(*runtime,owner,safe));assert(N::prepare(S::admission(),owner,safe));
  const bool retained=mode=="failed-close"||mode=="refresh-retained";
  assert(runtime->run()!=retained);assert(runtime->retained()==retained && starts==1 && refreshes==1);
  if(retained){
    if(mode=="refresh-retained")assert(!appCalls);
    assert(S::sdFiles().retained() && !stops);
    const auto calls=backend.totalCalls();uint32_t handle=99;
    assert(S::sdFiles().open("/file",RISC_FILES_READ,&handle)==RISC_FILES_RETAINED&&!handle);
    assert(backend.totalCalls()==calls);puts("Product startup terminal custody retained PASS");fflush(stdout);_exit(0);
  }
  assert(S::sdFiles().bound() && S::sdFiles().exitSafe() && !stops && !quiesces);
  assert(runtime->nativeFilesServiceSafe()&&!runtime->active()&&!risc_runtime_get_api(1));
  for(unsigned i=0;i<8;++i)assert(!runtime->run()&&runtime->nativeFilesServiceSafe());
  sequence=0;
  auto post=[&](const std::string& body,int expected=0){
    const std::string line="RTEF1 87654321 "+std::to_string(++sequence)+" "+body+"\n";
    for(unsigned char c:line){N::input(c);}
    assert(runtime->nativeFilesCheckpoint());
    std::string out;while(N::outputSize()){out.append(N::output(),N::outputSize());N::consumeOutput(N::outputSize());}
    const auto prefix="RTEF1 87654321 "+std::to_string(sequence)+" "+std::to_string(expected);
    assert(out.rfind(prefix,0)==0);return out;
  };
  post("HELLO");post("NS 0");post("STAT 12",RISC_FILES_INVALID);
  post("PATH 0 2f66696c65");post("STAT 12",mode=="absent-media"||mode=="refresh-refused"?RISC_FILES_UNAVAILABLE:0);
  if(mode=="failed-release"){
    assert(!runtime->stopNativeFiles()&&runtime->retained()&&!stops);
    const auto calls=backend.totalCalls();for(unsigned i=0;i<8;++i)assert(!runtime->stopNativeFiles()&&!runtime->nativeFilesCheckpoint());
    assert(backend.totalCalls()==calls&&S::sdFiles().retained());puts("Product final-stop failed release retains custody PASS");fflush(stdout);_exit(0);
  }
  assert(runtime->stopNativeFiles()&&!runtime->nativeFilesServiceSafe());
  for(unsigned i=0;i<8;++i)assert(runtime->stopNativeFiles()&&!runtime->run());
  assert(!S::sdFiles().bound() && S::sdFiles().exitSafe() && stops==1 && quiesces==1);
  delete runtime;printf("Product startup %s instance %u, wire namespace 12: %s PASS\n",TEST_PRODUCT_PROVIDER,TEST_PRODUCT_INSTANCE,mode.c_str());
}
