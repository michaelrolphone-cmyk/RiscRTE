// Dedicated transfer mode with actual Runtime, native storage, pinned LittleFS
// namespace 2 and the exact current product's async sdmmc-fatfs namespace 12.
// Only boot eligibility and physical/platform boundaries are modeled here.
#include "bootstrap/Runtime.h"
#include "ports/esp32s3/NativeSerialFiles.h"
#include "ports/esp32s3/NativeSerialStorageBinding.h"
#include "RuntimeFileFixture.h"
#include "native_littlefs_profile_fixture.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <unistd.h>
namespace N=RiscCpu::NativeSerialFiles;
namespace S=RiscCpu::NativeSerialStorage;
static RiscBoot::Runtime* runtime;
static const RiscBoot::RuntimeFileSources* metadata;
static std::string mode,sid="12345678";
static unsigned sequence,checkpoints,controls,polls,appCalls,transferEntries;
static bool owned=true,platformSafe=true,parked=false;
// Explicit host input. Actual bank confirmation/update-custody gate is qualified
// separately; this fixture never asserts anything about a persistent bank.
static bool eligible(){return true;}
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
    assert(parked&&!runtime->active()&&!risc_runtime_get_api(1)&&!appCalls);++checkpoints;
    return N::checkpoint();
}
static bool controlCheckpoint(){
    assert(!runtime->active()&&!risc_runtime_get_api(1)&&!appCalls);++controls;
    const auto before=counter(0);const bool result=N::controlCheckpoint();assert(counter(0)==before);
    if(!parked)assert(!N::transferActive()&&!N::outputSize());return result;
}
static void poll(){++polls;assert(!appCalls);}
static void enterTransferMode(){
    assert(parked&&runtime->nativeFilesServiceSafe()&&!runtime->active()&&!appCalls&&!runtime->retained());
    const bool active=N::transferActive();N::enterTransferMode();
    if(!active&&N::transferActive())++transferEntries;
}
static void input(const std::string& text){for(unsigned char byte:text)N::input(byte);}
static std::string drain(){std::string out;while(N::outputSize()){out.append(N::output(),N::outputSize());N::consumeOutput(N::outputSize());}return out;}
static std::string command(const std::string& text,int expected=0,bool duplicate=false){
    assert(parked&&!appCalls);
    const std::string line="RTEF1 "+sid+" "+std::to_string(++sequence)+" "+text+"\n";
    const unsigned before=counter(0);input(line);assert(counter(0)==before);
    const bool dispatched=runtime->nativeFilesCheckpoint();
    assert(dispatched==(expected!=RISC_FILES_RETAINED));
    const std::string out=drain();
    const std::string prefix="RTEF1 "+sid+" "+std::to_string(sequence)+" "+std::to_string(expected);
    if(out.rfind(prefix,0)!=0)std::fprintf(stderr,"%s -> %s expected %s\n",text.c_str(),out.c_str(),prefix.c_str());
    assert(out.rfind(prefix,0)==0&&(out[prefix.size()]==' '||out[prefix.size()]=='\n'));
    if(duplicate){const unsigned previous=counter(0);input(line);
        assert(runtime->nativeFilesCheckpoint());assert(drain()==out&&counter(0)==previous);}
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
static void upload(unsigned ns,const std::string& name,const std::string& bytes,const char* operation){
    path(name);command("PUT "+std::to_string(ns)+" "+operation+" "+std::to_string(bytes.size()),0,true);
    assert(metadata->exitSafe()&&!S::admission()->namespaces[ns==2?0:1].files->exitSafe());
    for(size_t i=0;i<bytes.size();i+=RiscSerial::Files::Chunk)
        command("WRITE "+std::to_string(i)+" "+hex(bytes.substr(i,RiscSerial::Files::Chunk)),0,true);
    command("COMMIT "+crc(bytes),0,true);
    assert(S::admission()->namespaces[ns==2?0:1].files->exitSafe()&&!N::exitSafe()&&N::custodySafe());
}
static void readback(unsigned ns,const std::string& name,const std::string& bytes){
    path(name);assert(command("GET "+std::to_string(ns)).find(" GET "+std::to_string(bytes.size())+"\n")!=std::string::npos);
    for(size_t i=0;i<bytes.size();i+=RiscSerial::Files::Chunk){
        const std::string part=bytes.substr(i,RiscSerial::Files::Chunk);
        assert(command("READ "+std::to_string(i),0,true).find(" READ "+std::to_string(i+part.size())+" "+hex(part)+"\n")!=std::string::npos);
    }
    assert(command("CLOSE",0,true).find(" CLOSE "+crc(bytes)+"\n")!=std::string::npos);
}
static void siblings(){
    std::string bytes;for(unsigned i=0;i<193;++i)bytes+=char(i*37u+11u);
    readback(12,"/Books/sibling.bin",bytes);readback(12,"/Books/nested/keep.txt","nested sibling");readback(12,"/unrelated.txt","root sibling");
}
static void privateNamespace(){
    const auto before=counter(0),writes=counter(2);std::string bytes;
    for(unsigned i=0;i<317;++i)bytes+=char(i*23u+17u);
    upload(2,"/unrelated.txt",bytes,"create");readback(2,"/unrelated.txt",bytes);
    path("/unrelated.txt");command("PUT 2 create 1",RISC_FILES_EXISTS);
    upload(2,"/unrelated.txt","private replacement","replace");readback(2,"/unrelated.txt","private replacement");
    upload(2,"/empty.bin","","create");readback(2,"/empty.bin","");
    path("/missing.bin");command("PUT 2 replace 1",RISC_FILES_NOT_FOUND);
    assert(counter(0)==before&&counter(2)==writes);serial_littlefs_preserved();
    puts("NAMESPACE2 actual_pinned_littlefs=1 create_commit_read_close=1 replace=1 zero_length=1 sd_io=0 app_and_settings_siblings_preserved=1 PASS");
}
extern "C" void test_atomic_app(unsigned,unsigned){++appCalls;assert(!"Early transfer intent must skip every app lifecycle hook");}
static void retainedExit(const char* reason){
    assert(runtime->retained()&&S::sdFiles().retained()&&!runtime->nativeFilesServiceSafe()&&!counter(5)&&!appCalls);
    const auto calls=counter(0),starts=counter(3),quiesces=counter(4);
    uint32_t handle=99;assert(S::sdFiles().open("/unrelated.txt",RISC_FILES_READ,&handle)==RISC_FILES_RETAINED&&!handle);
    for(unsigned i=0;i<8;++i){assert(!runtime->nativeFilesCheckpoint()&&!runtime->stopNativeFiles()&&!runtime->run());}
    N::disconnect();(void)N::checkpoint();assert(!N::shutdown());
    assert(counter(0)==calls&&counter(3)==starts&&counter(4)==quiesces&&!counter(5));
    std::printf("RETAINED case=%s reason=%s app_hooks=0 provider_retry=0 graph_stop=0 mapped_provider=%u PASS\n",mode.c_str(),reason,graphCounter!=nullptr);
    std::fflush(stdout);_exit(0);
}
int main(int argc,char**argv){
    assert(argc==3);std::setvbuf(stdout,nullptr,_IONBF,0);const std::string root=argv[1];mode=argv[2];
    std::printf("MODEL case=%s boot_eligible=explicit_fixture actual_bank_gate=separate_suite sd=actual_current_product_ram_fatfs ns2=actual_pinned_littlefs physical_qualification=0\n",mode.c_str());
    serial_littlefs_start();
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
    port.appExitSafe=safe;port.providerStorageSafe=safe;
    port.serialFilesCheckpoint=checkpoint;port.serialFilesControlCheckpoint=controlCheckpoint;
    port.serialFilesShutdown=N::shutdown;port.serialFilesStop=N::stop;
    port.serialTransferRequested=N::transferRequested;port.serialTransferEnter=enterTransferMode;port.serialFilesPoll=poll;
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
    if(mode=="cold")assert(runtime->bindNativeFiles("sdmmc-fatfs",12,S::sdFiles(),owner,safe,false,RiscBoot::Runtime::NativeBindingLifetime::Firmware));
    else assert(S::bindSd(*runtime,owner,safe));
    assert(!S::sdFiles().bound()&&counter(3)==0&&counter(0)==0);
    assert(N::prepare(S::admission(),owner,safe)&&N::enableTransferMode(eligible));
    input("RTEF1 "+sid+" 1 HELLO\n");sequence=1;
    assert(!N::transferRequested()&&!N::transferActive()&&!N::outputSize()&&counter(0)==0);
    const bool result=runtime->run();
    assert(N::transferRequested()&&!N::transferActive()&&!N::outputSize()&&!appCalls&&polls&&controls);
    if(mode=="failed-prepare"){
        assert(!result&&runtime->retained()&&S::sdFiles().retained()&&counter(3)==1&&counter(8)&&counter(9));
        assert(!checkpoints&&!transferEntries);retainedExit("failed_media_preparation");
    }
    if(!result)std::fprintf(stderr,"run: %s\n",runtime->error());assert(result&&!runtime->retained());
    parked=true;assert(!runtime->active()&&!risc_runtime_get_api(1)&&runtime->nativeFilesServiceSafe());
    assert(S::sdFiles().bound()&&counter(3)==1&&!counter(4)&&!counter(5));
    assert(!checkpoints&&!transferEntries&&N::pending());
    const auto beforeReady=counter(0);assert(runtime->nativeFilesCheckpoint());
    assert(drain()=="RTEF1 "+sid+" 1 0 HELLO 48 511 2\n");
    assert(N::transferActive()&&transferEntries==1&&checkpoints==1&&counter(0)==beforeReady);
    std::printf("EARLY_INTENT app_hooks=0 runtime_parked=1 pre_park_hello_success=0 parked_hello_success=1 transfer_entries=%u polls=%u controls=%u PASS\n",transferEntries,polls,controls);
    const auto beforeRerun=counter(0);for(unsigned i=0;i<8;++i)assert(!runtime->run()&&runtime->nativeFilesServiceSafe());
    assert(!appCalls&&counter(0)==beforeRerun);
    assert(command("NS 0").find(" NS 2 1 53657269616c2066696c6573\n")!=std::string::npos);
    assert(command("NS 1").find(" NS 12 1 53442063617264\n")!=std::string::npos);
    command("NS 2",RISC_FILES_NOT_FOUND);
    privateNamespace();
    if(mode=="cold"||mode=="absent-card"){
        const auto calls=counter(0),writes=counter(2);path("/unrelated.txt");
        for(unsigned i=0;i<8;++i){command("GET 12",RISC_FILES_UNAVAILABLE);command("PUT 12 create 2",RISC_FILES_UNAVAILABLE);}
        assert(counter(0)==calls&&counter(2)==writes&&!counter(7)&&N::custodySafe());
        if(mode=="cold")assert(!calls);
        readback(2,"/unrelated.txt","private replacement");
        assert(counter(0)==calls&&counter(2)==writes);serial_littlefs_preserved();
        std::printf("UNAVAILABLE case=%s repeated_namespace12_requests=16 provider_retry=0 namespace2_still_usable=1 PASS\n",mode.c_str());
    }else{
        native_lfs_fixture_counters before{},after{};native_lfs_fixture_get_counters(&before);
        siblings();std::string bytes;for(unsigned i=0;i<389;++i)bytes+=char(i*19u+3u);
        upload(12,"/Books/transfer.bin",bytes,"create");readback(12,"/Books/transfer.bin",bytes);
        path("/Books/transfer.bin");command("PUT 12 create 1",RISC_FILES_EXISTS);
        upload(12,"/Books/transfer.bin","replacement","replace");readback(12,"/Books/transfer.bin","replacement");
        upload(12,"/Books/empty.bin","","create");readback(12,"/Books/empty.bin","");
        path("/Books/missing.bin");command("PUT 12 replace 2",RISC_FILES_NOT_FOUND);siblings();
        native_lfs_fixture_get_counters(&after);
        for(unsigned i=0;i<NATIVE_LFS_OPERATION_COUNT;++i)assert(before.calls[i]==after.calls[i]);
        readback(2,"/unrelated.txt","private replacement");serial_littlefs_preserved();
        puts("NAMESPACE12 actual_current_sdmmc_fatfs=1 create_commit_read_close=1 replace=1 zero_length=1 littlefs_io=0 unrelated_siblings_preserved=1 PASS");
        if(mode=="failed-close"||mode=="failed-write"){
            path("/Books/transfer.bin");command("PUT 12 replace 2");command("WRITE 0 7879");fault(mode=="failed-close"?1:3);
            command(mode=="failed-close"?"ABORT":"COMMIT 8fe62899",RISC_FILES_RETAINED);
            assert(!N::custodySafe()&&counter(6));retainedExit(mode=="failed-close"?"post_park_failed_close":"post_park_failed_write");
        }
        if(mode=="disconnect"){
            path("/Books/transfer.bin");command("GET 12");const auto calls=counter(0);N::disconnect();
            assert(counter(0)==calls&&!N::exitSafe()&&N::transferActive());
            assert(runtime->nativeFilesCheckpoint()&&N::exitSafe()&&S::sdFiles().exitSafe()&&N::transferActive());
            puts("DISCONNECT checked_close=1 mode_stays_latched=1 app_restart=0 PASS");
        }
        for(unsigned i=0;i<16;++i){
            assert(N::shutdown()&&N::transferRequested()&&N::transferActive());sequence=0;sid=std::to_string(10000000+i);command("HELLO");
            readback(12,"/Books/transfer.bin","replacement");readback(2,"/unrelated.txt","private replacement");
            assert(runtime->nativeFilesServiceSafe()&&counter(3)==1&&!counter(4)&&!counter(5)&&transferEntries==1&&!appCalls);
            serial_littlefs_preserved();
        }
        puts("SESSION_RESET cycles=16 both_namespaces_readback=1 transfer_stays_latched=1 provider_restart=0 app_restart=0 PASS");
    }
    if(mode=="failed-release"){
        fault(2);assert(!runtime->stopNativeFiles());assert(runtime->retained()&&S::sdFiles().retained()&&!counter(5));
        // stop disabled the endpoint before final graph release failed.
        const auto calls=counter(0),quiesces=counter(4);
        for(unsigned i=0;i<8;++i)assert(!runtime->stopNativeFiles()&&!runtime->nativeFilesCheckpoint()&&!runtime->run());
        assert(counter(0)==calls&&counter(4)==quiesces&&graphCounter&&!appCalls);
        puts("RETAINED case=failed-release reason=final_graph_release app_hooks=0 provider_retry=0 graph_stop=0 mapped_provider=1 PASS");std::fflush(stdout);_exit(0);
    }
    if(mode=="healthy"){
        const auto calls=counter(0);protocolMs=RiscSerial::Files::TimeoutMs-1;
        assert(runtime->nativeFilesCheckpoint()&&!N::exitSafe());++protocolMs;
        assert(runtime->nativeFilesCheckpoint()&&N::exitSafe()&&N::transferActive()&&counter(0)==calls);
        assert(!runtime->run()&&!appCalls);puts("IDLE_EXPIRY session_cleared=1 mode_stays_latched=1 provider_io=0 app_restart=0 PASS");
    }
    assert(runtime->stopNativeFiles()&&!runtime->nativeFilesServiceSafe());const auto afterStop=counter(0);
    for(unsigned i=0;i<8;++i){assert(runtime->stopNativeFiles());assert(!runtime->run());}
    input("RTEF1 aaaaaaaa 1 HELLO\n");assert(!runtime->nativeFilesCheckpoint()&&drain().empty()&&counter(0)==afterStop);
    assert(!S::sdFiles().bound()&&S::sdFiles().exitSafe()&&counter(3)==1&&counter(4)==1&&counter(5)==1);
    for(unsigned i=6;i<=11;++i)assert(!counter(i));
    uint32_t handle=99;assert(S::sdFiles().open("/unrelated.txt",RISC_FILES_READ,&handle)==RISC_FILES_UNAVAILABLE&&!handle);
    delete runtime;assert(!dlclose(library));serial_littlefs_stop();
    std::printf("RESULT case=%s app_hooks=0 checked_final_stop=1 late_input_io=0 independent_namespaces=1 PASS\n",mode.c_str());
}
