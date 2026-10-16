#include "ports/esp32s3/NativeSerialStorage.h"
#include "ports/esp32s3/NativeSpiffsFiles.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
extern "C" {void fixture_mount();void fixture_unmount();uint32_t fixture_flash_digest();}
namespace A=RiscCpu::NativeSerialStorage;
static bool allowed=true;
int main(){
  fixture_mount();
  RiscCpu::NativeSpiffsFiles metadata("/bootfs");const auto& api=*metadata.api();
  uint32_t h=0,n=0;
  assert(!api.open(api.context,"/apps/default.elf",RISC_FILES_CREATE|RISC_FILES_WRITE,&h));
  assert(!api.write(api.context,h,"active executable",17,&n)&&n==17);assert(!api.close(api.context,h));
  assert(!api.open(api.context,"/apps/default.elf",RISC_FILES_READ,&h));
  const auto digest=fixture_flash_digest();
  assert(A::prepare([](){return true;},[](){return allowed;}));
  auto admission=A::admission();assert(admission&&admission->count==1&&admission->namespaces[0].id==1&&!admission->namespaces[0].writable);
  auto& client=*admission->namespaces[0].files;uint32_t handle=0,got=0;char bytes[32]{};
  assert(!client.open("/apps/default.elf",RISC_FILES_READ,&handle));
  assert(!client.read(handle,bytes,sizeof(bytes),&got)&&got==17&&!memcmp(bytes,"active executable",17));
  allowed=false;assert(client.close(handle)==RISC_FILES_BUSY);allowed=true;assert(!client.close(handle));
  assert(client.open("/apps/default.elf",RISC_FILES_WRITE|RISC_FILES_TRUNCATE,&handle)==RISC_FILES_DENIED);
  assert(client.open("/new",RISC_FILES_WRITE|RISC_FILES_CREATE,&handle)==RISC_FILES_DENIED);
  assert(client.remove("/apps/default.elf")==RISC_FILES_DENIED);
  assert(client.rename("/apps/default.elf","/other")==RISC_FILES_DENIED);
  assert(client.mkdir("/folder")==RISC_FILES_DENIED);
  assert(!api.read(api.context,h,bytes,sizeof(bytes),&got)&&got==17);assert(!api.close(api.context,h));
  assert(metadata.exitSafe()&&client.exitSafe()&&fixture_flash_digest()==digest);
  RiscSerial::Files endpoint;assert(endpoint.bind(admission->namespaces,1,{[](){return true;},[](){return allowed;},[](){return uint32_t(1);}}));
  const auto command=[&](const char* text){for(const char*p=text;*p;++p)endpoint.input(*p);endpoint.checkpoint();std::string out(endpoint.output(),endpoint.outputSize());endpoint.consumeOutput(endpoint.outputSize());return out;};
  assert(command("RTEF1 12345678 1 HELLO\n").find(" 0 HELLO")!=std::string::npos);
  assert(command("RTEF1 12345678 2 PATH 0 2f617070732f64656661756c742e656c66\n").find(" 0 PATH")!=std::string::npos);
  assert(command("RTEF1 12345678 3 PUT 1 replace 0\n").find(" "+std::to_string(RISC_FILES_DENIED))!=std::string::npos);
  assert(command("RTEF1 12345678 4 GET 1\n").find(" 0 GET 17")!=std::string::npos);
  assert(command("RTEF1 12345678 5 READ 0\n").find(" 0 READ 17")!=std::string::npos);
  assert(command("RTEF1 12345678 6 CLOSE\n").find(" 0 CLOSE")!=std::string::npos);
  assert(endpoint.shutdown()&&fixture_flash_digest()==digest);
  fixture_unmount();puts("Explicit read-only active store: independent sessions, wire read, mutation refusal, exact raw flash digest preserved PASS");
}
