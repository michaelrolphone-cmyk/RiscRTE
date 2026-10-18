#include "bootstrap/Runtime.h"
#include "ports/esp32s3/NativeSerialFiles.h"
#include "ports/esp32s3/NativeSerialStorageBinding.h"
#include "RuntimeFileFixture.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <unistd.h>
namespace N=RiscCpu::NativeSerialFiles;
namespace S=RiscCpu::NativeSerialStorage;
static RiscBoot::Runtime* runtime;
static const risc_runtime_api_v1* app;
static const RiscBoot::RuntimeFileSources* metadata;
static std::string mode;
static unsigned sequence,checkpoints,appCalls;
static bool owned=true,platformSafe=true,parked=false;
static std::string sid="12345678";
extern "C" void serial_littlefs_start();
extern "C" void serial_littlefs_preserved();
extern "C" void serial_littlefs_stop();
static unsigned (*transportCounter)(unsigned),(*graphCounter)(unsigned);
static unsigned graphSnapshot[13]{};
static unsigned counter(unsigned i){
    if(i==3||i==4||i==5||i==6||i==11||i==12)return graphCounter?graphCounter(i):graphSnapshot[i];
    return transportCounter(i);
}
extern "C" void wave_serial_graph_loaded(unsigned(*read)(unsigned)){assert(!graphCounter&&read);graphCounter=read;}
extern "C" void wave_serial_graph_unloaded(unsigned(*read)(unsigned)){
    assert(graphCounter==read);for(unsigned i=0;i<13;++i)graphSnapshot[i]=read(i);graphCounter=nullptr;
}
static void (*fault)(unsigned);
static const void* (*platform)(unsigned);
static bool owner(){return owned;}
static bool safe(){return platformSafe;}
static uint32_t protocolMs;
extern "C" int64_t esp_timer_get_time(){return int64_t(protocolMs)*1000;}
extern "C" bool xPortInIsrContext(){return false;}
static bool checkpoint(){
    assert(!runtime->active()&&!risc_runtime_get_api(1));++checkpoints;
    return N::checkpoint();
}
static std::string drain(){std::string out;while(N::outputSize()){out.append(N::output(),N::outputSize());N::consumeOutput(N::outputSize());}return out;}
static bool dispatch(){
    if(parked)return runtime->nativeFilesCheckpoint();
    return app->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_OK;
}
static std::string command(const std::string& text,int expected=0,bool duplicate=false){
    const std::string line="RTEF1 "+sid+" "+std::to_string(++sequence)+" "+text+"\n";
    const unsigned before=counter(0),count=checkpoints;
    for(unsigned char byte:line)N::input(byte);
    assert(counter(0)==before);
    if(!parked){app->yield_ms(1);assert(counter(0)==before&&checkpoints==count);}
    const bool dispatched=dispatch();
    if(dispatched!=(expected!=RISC_FILES_RETAINED))std::fprintf(stderr,"dispatch mismatch command=%s expected=%d output=%s\n",text.c_str(),expected,N::output()?N::output():"<none>");
    assert(dispatched==(expected!=RISC_FILES_RETAINED));
    const std::string out=drain();
    const std::string prefix="RTEF1 "+sid+" "+std::to_string(sequence)+" "+std::to_string(expected);
    if(out.rfind(prefix,0)!=0)std::fprintf(stderr,"%s -> %s expected %s\n",text.c_str(),out.c_str(),prefix.c_str());
    assert(out.rfind(prefix,0)==0&&(out[prefix.size()]==' '||out[prefix.size()]=='\n'));
    if(duplicate){const unsigned previous=counter(0);for(unsigned char byte:line)N::input(byte);
        assert(dispatch());assert(drain()==out&&counter(0)==previous);}
    return out;
}
static std::string hex(const std::string& bytes){
    static const char digit[]="0123456789abcdef";std::string out;
    for(unsigned char b:bytes){out+=digit[b>>4];out+=digit[b&15];}return out;
}
static std::string crc(const std::string& bytes){
    uint32_t value=0xffffffffu;for(unsigned char b:bytes){value^=b;for(unsigned bit=0;bit<8;++bit)value=(value>>1)^((value&1)?0xedb88320u:0);}
    char out[9];std::snprintf(out,sizeof(out),"%08x",value^0xffffffffu);return out;
}
static void path(const std::string& name){
    for(size_t i=0;i<name.size();i+=RiscSerial::Files::Chunk)
        command("PATH "+std::to_string(i)+" "+hex(name.substr(i,RiscSerial::Files::Chunk)));
}
static void upload(const std::string& name,const std::string& bytes,const char* operation){
    path(name);command("PUT 12 "+std::string(operation)+" "+std::to_string(bytes.size()),0,true);
    assert(!S::sdFiles().exitSafe()&&metadata->exitSafe());
    for(size_t i=0;i<bytes.size();i+=RiscSerial::Files::Chunk)
        command("WRITE "+std::to_string(i)+" "+hex(bytes.substr(i,RiscSerial::Files::Chunk)),0,true);
    command("COMMIT "+crc(bytes),0,true);assert(S::sdFiles().exitSafe()&&!N::exitSafe()&&N::custodySafe());
}
static void readback(const std::string& name,const std::string& bytes){
    path(name);assert(command("GET 12").find(" GET "+std::to_string(bytes.size())+"\n")!=std::string::npos);
    for(size_t i=0;i<bytes.size();i+=RiscSerial::Files::Chunk){
        const std::string part=bytes.substr(i,RiscSerial::Files::Chunk);
        assert(command("READ "+std::to_string(i),0,true).find(" READ "+std::to_string(i+part.size())+" "+hex(part)+"\n")!=std::string::npos);
    }
    assert(command("CLOSE",0,true).find(" CLOSE "+crc(bytes)+"\n")!=std::string::npos);
}
static void siblings(){
    std::string bytes;for(unsigned i=0;i<193;++i)bytes+=char(i*37u+11u);
    readback("/Books/sibling.bin",bytes);readback("/Books/nested/keep.txt","nested sibling");readback("/unrelated.txt","root sibling");
}
extern "C" void test_atomic_app(unsigned hook,unsigned){
    ++appCalls;app=risc_runtime_get_api(1);assert(app);
    if(hook!=1){assert(app->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_CONTEXT);return;}
    if(!S::sdFiles().bound()||counter(3)!=1)std::fprintf(stderr,"bound=%d starts=%u err=%s\n",S::sdFiles().bound(),counter(3),runtime->error());
    assert(S::sdFiles().bound()&&counter(3)==1);
    assert((counter(0)==0)==(mode=="cold"));
    risc_runtime_capability_v1 denied{sizeof(denied)};
    assert(!app->acquire("storage.files",1,12,&denied));assert(!app->acquire("storage.volume",1,12,&denied));
    assert(!runtime->bindNativeFiles("sdmmc-fatfs",12,S::sdFiles(),owner,safe));
    command("HELLO");assert(command("NS 0").find(" NS 2 1 53657269616c2066696c6573\n")!=std::string::npos);
    assert(command("NS 1").find(" NS 12 1 53442063617264\n")!=std::string::npos);
    path("/Books/selected.bin");
    if(mode=="post-return"||mode=="pipe"||mode=="post-failed-close"||mode=="default-run")return;
    if(mode=="cold"){
        command("GET 12",RISC_FILES_UNAVAILABLE);assert(counter(0)==0&&!counter(6));return;
    }
    if(mode=="absent-card"){
        const auto calls=counter(0),writes=counter(2);command("PUT 12 create 2",RISC_FILES_UNAVAILABLE);
        command("GET 12",RISC_FILES_UNAVAILABLE);
        assert(counter(0)==calls&&counter(2)==writes&&!counter(7)&&N::custodySafe());return;
    }
    siblings();
    std::string bytes;for(unsigned i=0;i<173;++i)bytes+=char(i*53u+7u);
    upload("/Books/selected.bin",bytes,"create");readback("/Books/selected.bin",bytes);
    path("/Books/selected.bin");command("PUT 12 create 1",RISC_FILES_EXISTS);
    upload("/Books/selected.bin","xy","replace");readback("/Books/selected.bin","xy");siblings();
    upload("/Books/empty.bin","","create");readback("/Books/empty.bin","");
    path("/Books/missing.bin");command("PUT 12 replace 2",RISC_FILES_NOT_FOUND);
    if(mode=="failed-close"||mode=="failed-write"){
        path("/Books/selected.bin");command("PUT 12 replace 2");command("WRITE 0 7879");
        fault(mode=="failed-close"?1:3);
        command(mode=="failed-close"?"ABORT":"COMMIT 8fe62899",RISC_FILES_RETAINED);
        assert(runtime->retained()&&S::sdFiles().retained()&&!N::custodySafe()&&counter(6));
        const auto calls=counter(0);N::disconnect();N::checkpoint();
        assert(!N::shutdown()&&counter(0)==calls);return;
    }
    if(mode=="disconnect"){
        path("/Books/selected.bin");command("GET 12");N::disconnect();
        const auto before=counter(0);assert(!N::exitSafe());app->yield_ms(1);assert(counter(0)==before);
        assert(app->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_OK&&N::exitSafe()&&S::sdFiles().exitSafe());
    }
    if(mode=="healthy"){
        const auto before=counter(0);assert(!N::exitSafe()&&N::custodySafe());
        protocolMs=RiscSerial::Files::TimeoutMs-1;
        assert(app->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_OK&&!N::exitSafe());
        ++protocolMs;
        assert(app->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_OK&&N::exitSafe()&&N::custodySafe());
        assert(counter(0)==before); // idle-session expiry performs no provider I/O
    }

}
int main(int argc,char**argv){
    assert(argc==3);const std::string root=argv[1];mode=argv[2];serial_littlefs_start();
    void* library=dlopen((root+"/driver.elf").c_str(),RTLD_NOW|RTLD_LOCAL);
    if(!library)std::fprintf(stderr,"dlopen: %s\n",dlerror());assert(library);
    auto seed=reinterpret_cast<void(*)()>(dlsym(library,"wave_serial_seed"));
    platform=reinterpret_cast<const void*(*)(unsigned)>(dlsym(library,"wave_serial_platform"));
    transportCounter=reinterpret_cast<unsigned(*)(unsigned)>(dlsym(library,"wave_serial_counter"));
    fault=reinterpret_cast<void(*)(unsigned)>(dlsym(library,"wave_serial_fault"));
    assert(seed&&platform&&transportCounter&&fault);seed();
    if(mode=="absent-card"||mode=="failed-prepare")fault(0);
    if(mode=="failed-prepare")fault(2);
    RiscBoot::Port port{owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;}};
    port.appExitSafe=safe;port.providerStorageSafe=safe;port.serialFilesCheckpoint=checkpoint;port.serialFilesShutdown=N::shutdown;port.serialFilesStop=N::stop;
    RiscTest::FileSources sources({root.c_str()});metadata=sources.sources();
    runtime=new RiscBoot::Runtime(port);assert(sources.bind(*runtime));
    for(unsigned i=0;i<4;++i){static const char*names[]={"platform.gpio","platform.clock","platform.sync","platform.storage-worker"};
        assert(runtime->registerPlatform(names[i],1,i==1?RiscBoot::Runtime::Scope::Global:RiscBoot::Runtime::Scope::Device,i==1?0:12,platform(i)));}
    assert(S::prepare(owner,safe));assert(S::admission()&&S::admission()->count==2);
    assert(S::admission()->namespaces[0].id==2&&S::admission()->namespaces[1].id==12);
    assert(S::admission()->namespaces[0].files!=&S::sdFiles()&&S::admission()->namespaces[1].files==&S::sdFiles());
    assert(!runtime->bindNativeFiles("sdmmc-fatfs",12,S::sdFiles(),owner,safe));
    const bool prepared=runtime->prepare(root.c_str());
    if(!prepared)std::fprintf(stderr,"prepare: %s\n",runtime->error());assert(prepared);
    assert(!runtime->bindNativeFiles("sdmmc-fatfs",13,S::sdFiles(),owner,safe));
    assert(!runtime->bindNativeFiles("wrong-provider",12,S::sdFiles(),owner,safe));
    if(mode=="cold")assert(runtime->bindNativeFiles("sdmmc-fatfs",12,S::sdFiles(),owner,safe,false));
    else if(mode=="default-run")assert(runtime->bindNativeFiles("sdmmc-fatfs",12,S::sdFiles(),owner,safe,true));
    else assert(S::bindSd(*runtime,owner,safe));
    assert(!S::sdFiles().bound()&&counter(3)==0&&counter(0)==0);
    assert(N::prepare(S::admission(),owner,safe));
    const bool retained=mode=="failed-close"||mode=="failed-write"||mode=="failed-prepare";
    const bool result=runtime->run();
    if(result==retained)std::fprintf(stderr,"run=%d retained=%d error=%s\n",result,retained,runtime->error());
    assert(result!=retained&&runtime->retained()==retained);
    if(retained){
        if(mode=="failed-prepare")assert(!appCalls);
        assert(S::sdFiles().retained()&&counter(3)==1&&counter(5)==0&&counter(8)&&counter(9));
        const auto before=counter(0);uint32_t h=99;assert(S::sdFiles().open("/unrelated.txt",RISC_FILES_READ,&h)==RISC_FILES_RETAINED&&!h);
        assert(counter(0)==before);std::printf("Waveshare async SD serial %s: terminal custody pinned PASS\n",mode.c_str());std::fflush(stdout);_exit(0);
    }
    parked=true;app=nullptr;
    assert(!runtime->active()&&!risc_runtime_get_api(1)&&N::exitSafe());
    assert(runtime->nativeFilesServiceSafe());
    const bool runScoped=mode=="cold"||mode=="default-run";
    assert(S::sdFiles().bound()!=runScoped&&counter(5)==unsigned(runScoped));
    for(unsigned i=0;i<8;++i){assert(!runtime->run());assert(runtime->nativeFilesServiceSafe());}
    // A pre-return SID has no surviving protocol authority or PATH.
    command("STAT 12",-10);
    sequence=0;sid="87654321";
    assert(command("HELLO").find(" HELLO 48 511 2\n")!=std::string::npos);
    command("NS 0");command("NS 1");command("NS 2",RISC_FILES_NOT_FOUND);
    command("STAT 12",RISC_FILES_INVALID); // old PATH was cleared on app return
    path("/unrelated.txt");
    command("STAT 12",runScoped||mode=="absent-card"?RISC_FILES_UNAVAILABLE:0);
    // Independent private namespace remains useful even without available SD.
    path("/unrelated.txt");command("PUT 2 create 7");command("WRITE 0 70726976617465");
    command("COMMIT "+crc("private"));path("/unrelated.txt");
    command("GET 2");assert(command("READ 0").find(" 70726976617465\n")!=std::string::npos);
    assert(command("CLOSE").find(" CLOSE "+crc("private")+"\n")!=std::string::npos);
    serial_littlefs_preserved();
    if(!runScoped&&mode!="absent-card"){
        readback("/unrelated.txt","root sibling");
        std::string bytes;for(unsigned i=0;i<389;++i)bytes+=char(i*19u+3u);
        upload("/Books/post-return.bin",bytes,"create");readback("/Books/post-return.bin",bytes);
        path("/Books/post-return.bin");command("PUT 12 create 1",RISC_FILES_EXISTS);
        path("/Books/never-created.bin");command("PUT 12 replace 2",RISC_FILES_NOT_FOUND);
        upload("/Books/post-return.bin","replacement","replace");readback("/Books/post-return.bin","replacement");
        siblings();
        if(mode=="post-failed-close"){
            path("/Books/post-return.bin");command("PUT 12 replace 2");command("WRITE 0 7879");fault(1);command("ABORT",RISC_FILES_RETAINED);
            const auto calls=counter(0);assert(runtime->retained()&&!runtime->nativeFilesServiceSafe());
            for(unsigned i=0;i<8;++i)assert(!runtime->nativeFilesCheckpoint()&&!runtime->stopNativeFiles());
            assert(counter(0)==calls&&S::sdFiles().retained()&&!counter(5));
            puts("Post-return failed close pins exact provider custody PASS");fflush(stdout);_exit(0);
        }
        if(mode=="pipe"){
            // Pipe-only transport for the actual Python Client, no serial device.
            assert(N::shutdown());std::puts("READY");std::fflush(stdout);
            char line[256];while(std::fgets(line,sizeof(line),stdin)){
                for(unsigned char* p=reinterpret_cast<unsigned char*>(line);*p;++p)N::input(*p);
                assert(runtime->nativeFilesCheckpoint());const auto out=drain();
                std::fwrite(out.data(),1,out.size(),stdout);std::fflush(stdout);
            }
        }else for(unsigned i=0;i<16;++i){
            assert(N::shutdown());sequence=0;sid=std::to_string(10000000+i);command("HELLO");
            readback("/Books/post-return.bin","replacement");
            assert(runtime->nativeFilesServiceSafe()&&counter(3)==1&&!counter(4)&&!counter(5));
            serial_littlefs_preserved();
        }
    }
    if(mode=="failed-release"){
        fault(2);assert(!runtime->stopNativeFiles());assert(runtime->retained()&&S::sdFiles().retained()&&!counter(5));
        const auto calls=counter(0);for(unsigned i=0;i<8;++i)assert(!runtime->stopNativeFiles()&&!runtime->nativeFilesCheckpoint());
        assert(counter(0)==calls);puts("Final native-service release failure retains exact custody PASS");fflush(stdout);_exit(0);
    }
    assert(runtime->stopNativeFiles());assert(!runtime->nativeFilesServiceSafe());
    const auto afterStop=counter(0);
    for(unsigned i=0;i<8;++i){assert(runtime->stopNativeFiles());assert(!runtime->run());}
    for(unsigned char c:std::string("RTEF1 aaaaaaaa 1 HELLO\n"))N::input(c);
    assert(!runtime->nativeFilesCheckpoint()&&drain().empty()&&counter(0)==afterStop);
    assert(!S::sdFiles().bound()&&S::sdFiles().exitSafe()&&counter(3)==1&&counter(4)==1&&counter(5)==1);
    for(unsigned i=6;i<=11;++i)assert(!counter(i));
    uint32_t h=99;assert(S::sdFiles().open("/unrelated.txt",RISC_FILES_READ,&h)==RISC_FILES_UNAVAILABLE&&!h);
    delete runtime;assert(!dlclose(library));serial_littlefs_stop();
    std::printf("Waveshare async SD serial %s: native instance 12, actual driver/FatFs, RAM media PASS\n",mode.c_str());
}
