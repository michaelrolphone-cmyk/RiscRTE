#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include "RuntimeFileFixture.h"
static RiscStorage::Worker mailbox;
static bool worker=false,poison=false,acquired=false;
static const char* mode(){const char*p=std::getenv("PROBE_MODE");return p?p:"success";}
static uint64_t token;
static RiscCpu::Port*cpu;
extern "C" void storage_work_test_begin(){unsigned char request=1;assert(mailbox.submit(&request,1,&token)==0);assert(!cpu->appExitSafe()&&cpu->providerStorageSafe());if(!strcmp(mode(),"poison"))poison=true;}
extern "C" void storage_work_test_result(int okay){
 acquired=okay;
 if(!std::getenv("EXPECT_OLD_FAILURE")&&strcmp(mode(),"poison"))assert(acquired);
 else assert(!acquired);
 if(!strcmp(mode(),"exit-pending"))return;
 worker=true;assert(mailbox.workerStep());worker=false;
 unsigned char data;uint32_t size;int32_t status;assert(mailbox.collect(token,&data,1,&size,&status)==0);
}
int main(int argc,char**argv){assert(argc==2);std::string root=argv[1];
 RiscCpu::StorageWorkBackend backend{&mailbox,[](){return true;},[](){return worker;},[](uint32_t){}};
 RiscCpu::Hardware h{};h.owner=[](){return !worker;};h.storageWork=&backend;h.httpSafe=[](){return !poison;};
 RiscCpu::Port native(h);cpu=&native;
 assert(mailbox.bind([](void*,const void*,uint32_t,void*out,uint32_t,uint32_t*n)->int32_t{assert(worker);*(unsigned char*)out=7;*n=1;return 0;},nullptr)==0);
 auto save=[&](const char*name,const char*text){std::ofstream(root+"/"+name)<<text;};
 save("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"test","buses":[],"devices":[]})");
 save("app.json",R"({"type":"application","id":"home","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"platform.clock","api":1}]})");
 save("boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"platform.clock","api":1,"instance_id":0}]}]})");
 RiscBoot::Port port{[](){return !worker;},[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},[](RiscBoot::Runtime&r){static const uint32_t table[]={1,8};return r.registerPlatform("platform.clock",1,RiscBoot::Runtime::Scope::Global,0,table);}};
 port.appExitSafe=[](){return cpu->appExitSafe();};port.providerStorageSafe=[](){return cpu->providerStorageSafe();};
 static RiscTest::FileSources rFiles0({root.c_str()});auto*r=new RiscBoot::Runtime(port);assert(rFiles0.bind(*r));if(!r->prepare(root.c_str())){puts(r->error());assert(false);}bool result=r->run();
 if(std::getenv("EXPECT_OLD_FAILURE")||strcmp(mode(),"success")){assert(!result&&r->retained());printf("Production retention fence PASS mode=%s original=%s\n",mode(),std::getenv("EXPECT_OLD_FAILURE")?"yes":"no");return 0;}
 assert(result&&!r->retained()&&acquired);assert(cpu->appExitSafe());
 // Real exit and real custody faults remain unsafe; this is not an idle bypass.
 storage_work_test_begin();assert(!cpu->appExitSafe());poison=true;assert(!cpu->providerStorageSafe());
 puts("Production Runtime acquisition during copied storage work succeeds; actual exit and native fault fences remain closed PASS");
}
