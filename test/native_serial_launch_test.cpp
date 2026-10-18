#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main storage_files_adapter_fixture_main
#include "storage_files_adapter_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include "bootstrap/Runtime.h"
#include "ports/esp32s3/NativeSerialFiles.h"
#include "RuntimeFileFixture.h"
#include <fstream>
#include <unistd.h>

namespace N=RiscCpu::NativeSerialFiles;
static GenericProvider provider;
static RiscBoot::RuntimeFiles files,absent;
static RiscBoot::Runtime* runtime;
static const risc_runtime_api_v1* app;
static std::string mode;
static unsigned sequence,dispatches,shutdowns,defaults,children,finis,readyChecks;
static uint32_t nowMs;
static bool nativeSettingsReady=true;
static bool owner(){return true;}
static bool safe(){return true;}
static bool exitSafe(){return N::custodySafe();}
static bool servicesReady(){++readyChecks;return N::exitSafe()&&nativeSettingsReady;}
extern "C" int64_t esp_timer_get_time(){return int64_t(nowMs)*1000;}
extern "C" bool xPortInIsrContext(){return false;}
static bool checkpoint(){assert(!runtime->active()&&!risc_runtime_get_api(1));++dispatches;return N::checkpoint();}
static bool shutdown(){++shutdowns;return N::shutdown();}
static void input(const std::string& bytes){for(unsigned char b:bytes)N::input(b);}
static std::string frame(const char* body){return "RTEF1 12345678 "+std::to_string(++sequence)+" "+body+"\n";}
static std::string drain(){std::string out;while(N::outputSize()){out.append(N::output(),N::outputSize());N::consumeOutput(N::outputSize());}return out;}
static std::string command(const char* body,int expected=0){
  input(frame(body));const auto before=dispatches;app->yield_ms(1);assert(dispatches==before);
  const auto rc=app->service_checkpoint();
  assert(rc==(mode=="null-hook"?RISC_RUNTIME_CHECKPOINT_OK:RISC_RUNTIME_CHECKPOINT_BUSY));
  const auto out=drain();const auto prefix="RTEF1 12345678 "+std::to_string(sequence)+" "+std::to_string(expected);
  if(out.rfind(prefix,0))fprintf(stderr,"%s -> %s expected %s\n",body,out.c_str(),prefix.c_str());
  assert(out.rfind(prefix,0)==0);return out;
}
static void refused(){
  const auto calls=provider.calls;const auto checks=readyChecks,dispatched=dispatches;
  for(unsigned i=0;i<3;++i){
    const bool accepted=app->request_launch("child.elf");
    printf("GUARD mode=%s attempt=%u accepted=%u session_exit_safe=%u\n",mode.c_str(),i,accepted,N::exitSafe());fflush(stdout);
    assert(!accepted);assert(runtime->active()&&!runtime->retained());
  }
  assert(provider.calls==calls&&dispatches==dispatched&&readyChecks>=checks+3&&!shutdowns);
}
static void launch(){assert(N::exitSafe());assert(app->request_launch("child.elf"));}
extern "C" void test_atomic_app(unsigned hook,unsigned child){
  app=risc_runtime_get_api(1);assert(app);
  if(hook!=1){if(hook==2)++finis;assert(app->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_CONTEXT);return;}
  if(child){
    ++children;assert(N::exitSafe()&&shutdowns==1);
    if(mode=="postcommit-arrival"){
      assert(!N::outputSize());puts("LIMIT first HELLO arriving after launch commitment still resets during runOne shutdown");
    }
    if(mode=="baseline-reset"||mode=="null-hook"){
      input(frame("STAT 1"));assert(app->service_checkpoint()==(mode=="null-hook"?RISC_RUNTIME_CHECKPOINT_OK:RISC_RUNTIME_CHECKPOINT_BUSY));
      const auto out=drain();assert(out.find(" -10\n")!=std::string::npos);
      printf("BASELINE accepted launch ran child; shutdown=%u; previous SID/PATH lost; next STAT=%s",shutdowns,out.c_str());fflush(stdout);
    }
    return;
  }
  if(++defaults!=1)return;
  assert(N::exitSafe()&&app->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_OK);
  if(mode=="no-session"){launch();return;}
  if(mode=="postcommit-arrival"){
    launch();input(frame("HELLO"));assert(!N::exitSafe());
    const auto before=dispatches;assert(app->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_BUSY&&dispatches==before);return;
  }
  if(mode=="settings-busy"){
    nativeSettingsReady=false;assert(N::exitSafe());refused();
    assert(app->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_BUSY);
    nativeSettingsReady=true;assert(app->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_OK);launch();return;
  }
  if(mode=="partial-frame"){
    input("RTEF1 12345678 1 HEL");refused();input("LO\n");sequence=1;
    assert(app->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_BUSY);assert(drain().find(" 0 HELLO ")!=std::string::npos);
  }else if(mode=="pending-request"||mode=="stale-ready"){
    input(frame("HELLO"));
    // Input arrived after the last app-visible OK. Only the settled Runtime
    // checkpoint may dispatch; launch admission must inspect the new bytes.
    refused();assert(app->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_BUSY);
    assert(drain().find(" 0 HELLO ")!=std::string::npos);
  }else if(mode=="pending-reply"||mode=="partial-reply"){
    input(frame("HELLO"));assert(app->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_BUSY);
    std::string sent;
    if(mode=="partial-reply"){assert(N::outputSize()>8);sent.assign(N::output(),8);N::consumeOutput(8);}
    refused();assert((sent+drain()).find(" 0 HELLO ")!=std::string::npos);
  }else command("HELLO");
  assert(command("NS 2").find(" NS 12 ")!=std::string::npos);
  command("PATH 0 2f66696c65");
  assert(!N::pending()&&!N::exitSafe()&&N::custodySafe()&&files.exitSafe());
  if(mode=="baseline-reset"||mode=="null-hook"){
    const bool accepted=app->request_launch("child.elf");assert(accepted);
    const auto before=dispatches;assert(app->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_BUSY&&dispatches==before);
    puts("BASELINE drained PATH session active; request_launch accepted; queued checkpoint cannot dispatch");fflush(stdout);return;
  }
  if(mode=="return-shutdown"){command("GET 1");assert(!files.exitSafe());return;}
  refused();
  assert(command("STAT 1").find(" 0 STAT ")!=std::string::npos); // Refusal did not queue or erase PATH.
  if(mode=="unavailable-media"){
    command("STAT 12",RISC_FILES_UNAVAILABLE);command("GET 12",RISC_FILES_UNAVAILABLE);
    assert(command("NS 0").find(" NS 1 ")!=std::string::npos);
  }
  if(mode=="read"||mode=="disconnect-open"||mode=="failed-close"){
    command("GET 1");assert(!files.exitSafe());refused();
    if(mode=="failed-close"){provider.results["close"]=RISC_FILES_IO;return;}
    if(mode=="disconnect-open"){
      N::disconnect();assert(!N::exitSafe());refused();
      assert(app->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_OK&&files.exitSafe());launch();return;
    }
    assert(command("READ 0").find("616263646566")!=std::string::npos);command("CLOSE");refused();
  }
  if(mode=="write"){
    command("PUT 1 replace 2");refused();command("WRITE 0 7879");command("COMMIT 8fe62899");refused();
    assert(provider.files["/file"].bytes=="xy");
  }
  if(mode=="expiry"){
    nowMs=RiscSerial::Files::TimeoutMs-1;assert(app->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_BUSY);refused();
    ++nowMs;assert(app->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_OK&&N::exitSafe());
  }else N::disconnect();
  assert(app->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_OK);launch();
}
static void write(const std::string& path,const char* bytes){std::ofstream(path)<<bytes;}
int main(int argc,char**argv){
  assert(argc==3);const std::string root=argv[1];mode=argv[2];
  write(root+"/board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
  write(root+"/boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[]})");
  assert(files.bind(&provider.api,owner,safe));
  RiscSerial::Namespace namespaces[]={{1,"Boot files",&files,true},{2,"Private files",&files,true},{12,"Unavailable SD",&absent,true}};
  N::Admission admission{namespaces,3,safe};assert(N::prepare(&admission,owner,safe));
  RiscBoot::Port port{owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;}};
  port.appExitSafe=exitSafe;port.providerStorageSafe=exitSafe;
  port.serialFilesCheckpoint=checkpoint;port.serialFilesControlCheckpoint=N::controlCheckpoint;port.serialFilesShutdown=shutdown;port.serialFilesStop=N::stop;
  port.serialFilesReady=[](){return nativeSettingsReady;};
  if(mode!="null-hook")port.sleepServicesReady=servicesReady;
  RiscTest::FileSources sources({root});runtime=new RiscBoot::Runtime(port);assert(sources.bind(*runtime)&&runtime->prepare(root.c_str()));
  const bool result=runtime->run();
  if(mode=="failed-close"){
    assert(!result&&runtime->retained()&&files.retained()&&!N::custodySafe()&&!finis&&!children&&shutdowns==1);
    const auto before=provider.calls;assert(!N::shutdown());assert(provider.calls==before);
    puts("Failed natural-return serial close retained app and client; no fini/child/retry PASS");fflush(stdout);_exit(0);
  }
  assert(result&&!runtime->retained()&&N::exitSafe()&&files.exitSafe());
  if(mode=="return-shutdown")assert(defaults==1&&!children&&finis==1&&shutdowns==2);
  else assert(defaults==2&&children==1&&finis==3&&shutdowns==4);
  delete runtime;printf("Native Runtime launch boundary %s PASS\n",mode.c_str());
}
