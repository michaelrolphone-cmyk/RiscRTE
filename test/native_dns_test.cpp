#define RISC_NATIVE_ESP_IDF 1
#include "ports/esp32s3/NativeDns.h"
#include <cassert>
#include <cstdio>
static void(*queued)(void*)=nullptr;static bool fail=false;static int dns=0,deletes=0,frees=0;
int xTaskCreatePinnedToCore(void(*work)(void*),const char*,unsigned stack,void*,unsigned,void*,int core){assert(stack==4096&&core==0);if(fail)return 0;queued=work;return pdPASS;}
void vTaskDelete(void*){deletes++;}
extern "C" int __wrap_getaddrinfo(const char* host,const char* service,const addrinfo* hints,addrinfo** found){assert(!service&&hints->ai_family==AF_INET&&hints->ai_socktype==SOCK_STREAM);assert(!strcmp(host,"endpoint.test"));static sockaddr_in address{};address.sin_family=AF_INET;inet_pton(AF_INET,"192.0.2.1",&address.sin_addr);static addrinfo result{};result.ai_family=AF_INET;result.ai_addrlen=sizeof(address);result.ai_addr=reinterpret_cast<sockaddr*>(&address);*found=dns?nullptr:&result;return dns;}
extern "C" void __wrap_freeaddrinfo(addrinfo*){frees++;}
int main(){using namespace RiscCpu::NativeDns;sockaddr_in out{};fail=true;assert(resolve("endpoint.test",out)==-1&&!pending());fail=false;char name[]="endpoint.test";assert(resolve(name,out)==1&&pending());name[0]='X';assert(!discard()&&resolve("endpoint.test",out)==1);assert(resolve("other.test",out)==-1);queued(nullptr);assert(!pending()&&deletes==1&&frees==1);assert(resolve("endpoint.test",out)==0);char ip[16];assert(inet_ntop(AF_INET,&out.sin_addr,ip,sizeof(ip))&&!strcmp(ip,"192.0.2.1"));dns=EAI_NONAME;assert(resolve("endpoint.test",out)==1);queued(nullptr);assert(resolve("endpoint.test",out)==-1&&discard());puts("Native DNS: copied host, worker delay/cancel, one-slot exclusion, creation/DNS errors and address ownership PASS");}
