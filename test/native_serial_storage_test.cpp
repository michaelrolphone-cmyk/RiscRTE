#include "ports/esp32s3/NativeSerialStorage.cpp" // Inspect real raw workspace without a firmware test hook.
#include "ports/esp32s3/NativeAppDataMount.h"
#include "runtime/storage/AppDataFiles.h"
#include "runtime/storage/ReopenedFiles.h"
#include "native_littlefs_profile_fixture.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <vector>
#include <unistd.h>
namespace A=RiscCpu::NativeSerialStorage;
namespace M=RiscAppDataMount;
static bool owned=true,allowed=true,radio=false;
static uint32_t now=1;
extern "C" void native_lfs_fixture_select_appdata(void);
extern "C" void* xTaskGetCurrentTaskHandle(){static int owner;return &owner;}
extern "C" int xPortInIsrContext(){return 0;}
extern "C" bool native_mount_dram(const void*){return true;}
namespace RiscCpu {
bool nativeBackgroundResourceTry(){if(radio)return false;radio=true;return true;}
void nativeBackgroundResourceEnd(){assert(radio);radio=false;}
}
static bool owner(){return owned;}
static bool safe(){return allowed&&!M::retained();}
static native_lfs_fixture_counters counts(){native_lfs_fixture_counters c{};native_lfs_fixture_get_counters(&c);return c;}
static void noPhysicalHandles(){auto c=counts();assert(!c.live_files&&!c.live_dirs&&!M::held()&&!M::retained()&&!radio);}
static std::string hex(const std::string& in){std::string out;const char*digits="0123456789abcdef";for(unsigned char c:in){out+=digits[c>>4];out+=digits[c&15];}return out;}
static uint32_t crc(const std::string& in){uint32_t c=~0u;for(unsigned char b:in){c^=b;for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
struct Wire {
  RiscSerial::Files& files;unsigned sequence=0;
  std::string drain(){std::string result;while(files.outputSize()){size_t n=files.outputSize()>3?3:files.outputSize();result.append(files.output(),n);files.consumeOutput(n);}return result;}
  void feed(const std::string& text){const auto before=counts();for(unsigned char c:text)files.input(c);const auto after=counts();assert(!memcmp(&before,&after,sizeof(before)));}
  std::string command(const std::string& body,int expected=0){
    feed("RTEF1 abcdef01 "+std::to_string(++sequence)+" "+body+"\n");files.checkpoint();auto result=drain();
    const auto prefix="RTEF1 abcdef01 "+std::to_string(sequence)+" "+std::to_string(expected);
    if(result.rfind(prefix,0))fprintf(stderr,"command %s expected %s got %s\n",body.c_str(),prefix.c_str(),result.c_str());
    assert(result.rfind(prefix,0)==0&&(result[prefix.size()]==' '||result[prefix.size()]=='\n'));
    if(expected!=RISC_FILES_RETAINED)noPhysicalHandles();return result;
  }
  void path(const std::string& value){for(size_t i=0;i<value.size();i+=48)command("PATH "+std::to_string(i)+" "+hex(value.substr(i,48)));}
  void put(const std::string& pathValue,const std::string& bytes,const char* mode="create"){
    path(pathValue);command("PUT 2 "+std::string(mode)+" "+std::to_string(bytes.size()));
    for(size_t i=0;i<bytes.size();i+=48)command("WRITE "+std::to_string(i)+" "+hex(bytes.substr(i,48)));
    char digest[9];snprintf(digest,sizeof(digest),"%08x",crc(bytes));command("COMMIT "+std::string(digest));
  }
  std::string get(const std::string& pathValue){
    path(pathValue);const auto opened=command("GET 2");const auto size=std::stoull(opened.substr(opened.find(" GET ")+5));
    std::string encoded;for(size_t at=0;at<size;){auto value=command("READ "+std::to_string(at));auto pos=value.find(" READ ")+6;auto next=value.find(' ',pos);auto end=std::stoul(value.substr(pos,next-pos));assert(end>at);encoded+=value.substr(next+1,(end-at)*2);at=end;}
    command("CLOSE");return encoded;
  }
};
static std::string existing(const char* name,M::Access access){
  M::Guard guard(access);assert(guard.held);int fd=native_lfs_fixture_open(name,O_RDONLY);assert(fd>=0);
  char data[256];auto size=native_lfs_fixture_read(fd,data,sizeof(data));assert(size>=0);assert(native_lfs_fixture_close(fd)==0);return {data,size_t(size)};
}
static void preserved(){
  { M::Guard guard(M::Access::AppData);assert(guard.held);
    RiscStorage::AppDataFiles app({nullptr,[](void*){return now;},[](void*){return M::held();},malloc,free});
    assert(app.configure("/appdata"));uint32_t size=0;uint64_t revision=0;char bytes[32]{};
    assert(app.stat(1,"original",&size,&revision)==RISC_APP_DATA_OK);
    assert(app.read(1,"original",revision,bytes,sizeof(bytes),&size,&revision)==RISC_APP_DATA_OK&&size==11&&!memcmp(bytes,"app sibling",11));
  }
  assert(existing("/n00000001/original",M::Access::AppData)=="app sibling");
  assert(existing("/.native-settings/original",M::Access::Settings)=="settings sibling");
  noPhysicalHandles();
}
int main(int argc,char** argv){
  assert(argc==2);std::string scenario=argv[1];
  std::memset(A::privateProviderStorage,0xa5,sizeof(A::privateProviderStorage));
  std::memset(A::sessionStorage,0xa5,sizeof(A::sessionStorage));
  assert(!A::admission());owned=false;assert(!A::prepare(owner,safe));owned=true;
  for(auto byte:A::privateProviderStorage)assert(byte==0xa5);
  for(auto byte:A::sessionStorage)assert(byte==0xa5);
  if(scenario=="unsafe-prepare"){
    allowed=false;assert(!A::prepare(owner,safe));allowed=true;
    assert(!A::prepare(owner,safe) && !A::admission());
    for(auto byte:A::privateProviderStorage)assert(byte==0xa5);
  for(auto byte:A::sessionStorage)assert(byte==0xa5);
    puts("Serial provider unsafe admission remains unconstructed and one-shot PASS");return 0;
  }
  assert(native_lfs_fixture_reset()==0);
  assert(native_lfs_fixture_mkdir("/n00000001")==0);
  assert(native_lfs_fixture_seed("/n00000001/original","app sibling",11)==0);
  assert(native_lfs_fixture_mkdir("/.native-settings")==0);
  assert(native_lfs_fixture_seed("/.native-settings/original","settings sibling",16)==0);
  native_lfs_fixture_shutdown();native_lfs_fixture_select_appdata();
  assert(M::prepare(static_cast<const esp_partition_t*>(native_lfs_fixture_partition())));
  assert(native_lfs_fixture_reboot()==0&&M::finish());
  if(scenario=="first-full"){
    M::Guard g(M::Access::AppData);assert(g.held);std::vector<unsigned char> fill(380000,0x5a);assert(native_lfs_fixture_seed("/n00000001/fill",fill.data(),fill.size())==0);
  }
  if(scenario=="corrupt-root"){
    {M::Guard g(M::Access::SerialFiles);assert(g.held);assert(native_lfs_fixture_seed("/.serial-files","occupied",8)==0);}
    assert(!A::prepare(owner,safe)&&!A::admission());
    unsigned char failed[sizeof(A::privateProviderStorage)];std::memcpy(failed,A::privateProviderStorage,sizeof(failed));
    assert(!A::prepare(owner,safe) && !std::memcmp(failed,A::privateProviderStorage,sizeof(failed)));
    noPhysicalHandles();puts("Corrupt serial root fails closed PASS");return 0;
  }
  if(scenario=="first-busy")radio=true;
  assert(A::prepare(owner,safe));assert(!A::prepare(owner,safe));
  assert(reinterpret_cast<uintptr_t>(&A::privateProvider())%alignof(RiscStorage::ReopenedFiles)==0);
  if(scenario=="first-busy")radio=false;
  noPhysicalHandles();
  const auto* admission=A::admission();assert(admission&&admission->count==1+RISC_NATIVE_SERIAL_SD);
  assert(admission->namespaces[0].id==2&&admission->namespaces[0].writable&&!strcmp(admission->namespaces[0].label,"Serial files"));
  RiscSerial::Files endpoint;assert(endpoint.bind(admission->namespaces,admission->count,{owner,safe,[](){return now;}}));
  Wire wire{endpoint};wire.command("HELLO");
#if RISC_NATIVE_SERIAL_SD
  assert(&A::sdFiles()==admission->namespaces[1].files&&!A::sdFiles().bound());
  wire.path("/missing");wire.command("GET 12",RISC_FILES_UNAVAILABLE);
#endif
  if(scenario=="first-full"||scenario=="first-busy"){
    assert(!admission->namespaces[0].files->bound());
    wire.path("/selected");wire.command("PUT 2 create 1",RISC_FILES_UNAVAILABLE);
    wire.command("GET 2",RISC_FILES_UNAVAILABLE);preserved();assert(endpoint.shutdown());
  }else if(scenario=="healthy"){
    wire.put("/sibling","untouched");std::string bytes;for(unsigned i=0;i<779;++i)bytes+=char(i);
    wire.put("/selected",bytes);assert(wire.get("/selected")==hex(bytes));
    wire.path("/selected");wire.command("PUT 2 create 1",RISC_FILES_EXISTS);
    wire.put("/selected","tiny","replace");assert(wire.get("/selected")==hex("tiny"));
    wire.path("/selected");wire.command("PUT 2 replace 8");preserved();
    wire.command("WRITE 0 6162");preserved();
    const auto replayBefore=counts();wire.feed("RTEF1 abcdef01 "+std::to_string(wire.sequence)+" WRITE 0 6162\n");endpoint.checkpoint();assert(!wire.drain().empty());const auto replayAfter=counts();assert(!memcmp(&replayBefore,&replayAfter,sizeof(replayBefore)));
    wire.command("ABORT");assert(wire.get("/selected")=="6162");
    wire.put("/empty","");assert(wire.get("/empty").empty());
    wire.path("/folder");wire.command("MKDIR 2");wire.put("/folder/keep","nested");
    wire.path("/folder");wire.command("PUT 2 replace 1",RISC_FILES_DENIED);
    wire.path("/../n00000001/original");wire.command("GET 2",RISC_FILES_INVALID);
    auto& client=*admission->namespaces[0].files;uint32_t dir=0;assert(client.dirOpen("/",&dir)==0);unsigned entries=0;
    for(;;){risc_storage_files_entry_v1 entry{};entry.struct_size=sizeof(entry);auto rc=client.dirNext(dir,&entry);noPhysicalHandles();if(rc==RISC_FILES_END)break;assert(rc==0);++entries;assert(entries<10);preserved();}
    assert(client.dirClose(dir)==0&&entries==4);
    assert(wire.get("/sibling")==hex("untouched")&&wire.get("/folder/keep")==hex("nested"));
    {M::Guard g(M::Access::AppData);assert(g.held);struct stat st{};assert(native_lfs_fixture_stat("/.serial-files/selected",&st)<0);}
    {M::Guard g(M::Access::SerialFiles);assert(g.held);struct stat st{};assert(native_lfs_fixture_stat("/n00000001/original",&st)<0&&native_lfs_fixture_stat("/.native-settings/original",&st)<0);}
    preserved();assert(endpoint.shutdown());noPhysicalHandles();
  }else if(scenario=="busy"){
    wire.path("/selected");const auto before=counts();
    wire.feed("RTEF1 abcdef01 "+std::to_string(++wire.sequence)+" PUT 2 create 2\n");
    allowed=false;endpoint.checkpoint();assert(wire.drain().empty());allowed=true;owned=false;endpoint.checkpoint();owned=true;
    const auto after=counts();assert(!memcmp(&before,&after,sizeof(before)));
    endpoint.checkpoint();assert(wire.drain().find(" 0 PUT")!=std::string::npos);noPhysicalHandles();
    {M::Guard g(M::Access::AppData);assert(g.held);const auto calls=counts();
      wire.feed("RTEF1 abcdef01 "+std::to_string(++wire.sequence)+" WRITE 0 6162\n");endpoint.checkpoint();
      assert(wire.drain().find(" "+std::to_string(RISC_FILES_BUSY))!=std::string::npos);const auto later=counts();assert(!memcmp(&calls,&later,sizeof(calls)));}
    noPhysicalHandles();wire.command("ABORT");wire.put("/selected","ab","replace");preserved();
  }else if(scenario=="capacity"){
    {M::Guard g(M::Access::SerialFiles);assert(g.held);std::vector<unsigned char> fill(380000,0x5a);assert(native_lfs_fixture_seed("/.serial-files/fill",fill.data(),fill.size())==0);}
    wire.path("/denied");wire.command("PUT 2 create 1",RISC_FILES_NO_SPACE);preserved();
  }else if(scenario=="directory-limit"){
    {M::Guard g(M::Access::SerialFiles);assert(g.held);for(unsigned i=0;i<129;++i){char path[80];snprintf(path,sizeof(path),"/.serial-files/f%03u",i);assert(native_lfs_fixture_seed(path,"x",1)==0);}}
    auto& client=*admission->namespaces[0].files;uint32_t dir=0;assert(client.dirOpen("/",&dir)==0);
    risc_storage_files_entry_v1 entry{};entry.struct_size=sizeof(entry);for(unsigned i=0;i<128;++i){assert(client.dirNext(dir,&entry)==0);noPhysicalHandles();}
    assert(client.dirNext(dir,&entry)==RISC_FILES_UNAVAILABLE);assert(client.dirClose(dir)==0);preserved();
  }else{
    wire.put("/selected","abcdef");wire.path("/selected");
    if(scenario=="close")native_lfs_fixture_fault(NATIVE_LFS_CLOSE,1,NATIVE_LFS_FAIL_BEFORE);
    else if(scenario=="sync")native_lfs_fixture_fault(NATIVE_LFS_FSYNC,1,NATIVE_LFS_FAIL_BEFORE);
    if(scenario=="close")wire.command("GET 2",RISC_FILES_RETAINED);
    else if(scenario=="sync")wire.command("PUT 2 replace 2",RISC_FILES_RETAINED);
    else{
      wire.command("GET 2");
      if(scenario=="read")native_lfs_fixture_fault(NATIVE_LFS_READ,1,NATIVE_LFS_FAIL_BEFORE);
      else{assert(scenario=="changed-size");M::Guard g(M::Access::SerialFiles);assert(g.held);assert(native_lfs_fixture_seed("/.serial-files/selected","changed",7)==0);}
      wire.command("READ 0",RISC_FILES_RETAINED);
    }
    assert(!endpoint.custodySafe()&&M::retained()&&radio);const auto before=counts();
    unsigned char retained[sizeof(A::privateProviderStorage)];std::memcpy(retained,A::privateProviderStorage,sizeof(retained));
    assert(!A::prepare(owner,safe) && !std::memcmp(retained,A::privateProviderStorage,sizeof(retained)));
    for(unsigned i=0;i<8;++i){endpoint.checkpoint();assert(!endpoint.shutdown());}
    const auto after=counts();assert(!memcmp(&before,&after,sizeof(before)));
  }
  printf("Serial private native LittleFS namespace product=%s scenario=%s PASS\n",RISC_NATIVE_SERIAL_SD?"waveshare":"twatch",scenario.c_str());
}
