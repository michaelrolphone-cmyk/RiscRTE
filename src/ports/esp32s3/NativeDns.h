#pragma once
// Native-owned DNS mailbox. Resolver work holds no ELF pointer or callback.
// The owner copies one result and alone advances TLS/SNTP or closes sockets.
#if RISC_NATIVE_ESP_IDF
#include <atomic>
#include <cstring>
#include <netdb.h>
#include <arpa/inet.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
namespace RiscCpu { namespace NativeDns {
struct Mailbox {std::atomic<unsigned> state{0};char host[254]{};sockaddr_in address{};int result=-1;};
static Mailbox mailbox;
inline void worker(void*){
 addrinfo hints{};hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;addrinfo* found=nullptr;
 int code=::getaddrinfo(mailbox.host,nullptr,&hints,&found);
 if(!code&&found&&found->ai_addr&&found->ai_addrlen>=sizeof(sockaddr_in)&&found->ai_family==AF_INET)std::memcpy(&mailbox.address,found->ai_addr,sizeof(sockaddr_in));else code=-1;
 if(found)::freeaddrinfo(found);
 mailbox.result=code;
 mailbox.state.store(2,std::memory_order_release);
 // No mailbox access follows publication. Its storage is permanent native
 // memory; FreeRTOS owns and reclaims this worker's stack after deletion.
 vTaskDelete(nullptr);
}
inline bool pending(){return mailbox.state.load(std::memory_order_acquire)==1;}
inline bool discard(){if(pending())return false;mailbox.state.store(0,std::memory_order_release);return true;}
inline int resolve(const char* host,sockaddr_in& out){
 const unsigned state=mailbox.state.load(std::memory_order_acquire);
 if(!state){if(!host||std::strlen(host)>253)return -1;std::strcpy(mailbox.host,host);mailbox.result=-1;mailbox.state.store(1,std::memory_order_release);
  if(xTaskCreatePinnedToCore(worker,"risc-dns",4096,nullptr,1,nullptr,0)!=pdPASS){mailbox.state.store(0,std::memory_order_release);return -1;}return 1;}
 if(std::strcmp(mailbox.host,host))return -1;
 if(state==1)return 1;
 const int result=mailbox.result;if(!result)out=mailbox.address;mailbox.state.store(0,std::memory_order_release);return result? -1:0;
}
} }
#endif
