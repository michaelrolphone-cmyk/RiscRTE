#pragma once
// ESP-IDF lwIP boundary. The production code is also compiled with a syscall
// shim in host tests; those tests create no host sockets or network traffic.
#include "bootstrap/TcpListenerBackend.h"
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
namespace RiscCpu { namespace NativeTcpListener {
struct Socket {
 int fd=-1; uint64_t owner=0,token=0; bool listener=false;
};
static Socket sockets[RISC_TCP_LISTENERS_MAX+RISC_TCP_CLIENTS_MAX];
static uint64_t generation=0;
static bool retained=false,busy=false;
static bool (*ownerTask)()=nullptr;
static bool (*networkReady)()=nullptr;
inline bool owned(){return ownerTask && ownerTask();}
inline bool safe(void*){return !retained;}
inline bool idle(void*,uint64_t owner){
 if(retained)return false;
 for(const auto& s:sockets)if(s.token && (!owner || s.owner==owner))return false;
 return true;
}
inline void configure(bool (*owner)(),bool (*network)()){
 // Never overwrite custody or change native owner while resources exist.
 if(!idle(nullptr,0) || busy)return;
 ownerTask=owner;networkReady=network;
}
inline Socket* find(uint64_t owner,uint64_t token){
 if(!owner || !token)return nullptr;
 for(auto& s:sockets)if(s.owner==owner && s.token==token)return &s;
 return nullptr;
}
inline int32_t gate(){
 if(!owned() || busy)return RISC_TCP_CONTEXT;
 return retained?RISC_TCP_RETAINED:RISC_TCP_OK;
}
inline bool checked(){if(owned())return true;retained=true;return false;}
inline int32_t network(){
 const bool ready=networkReady && networkReady();
 if(!checked())return RISC_TCP_RETAINED;
 return ready?RISC_TCP_OK:RISC_TCP_NETWORK_DOWN;
}
struct Operation {Operation(){busy=true;}~Operation(){busy=false;}};
inline bool wouldBlock(int e){return e==EAGAIN || e==EWOULDBLOCK;}
inline int32_t destroy(Socket& s){
 if(!checked())return RISC_TCP_RETAINED;
 const int result=::close(s.fd);
 // A failed close may already have disposed/reused the descriptor. Never
 // retry it, clear custody, or claim that this invocation can unload.
 if(result || !checked()){retained=true;return RISC_TCP_RETAINED;}
 s=Socket{};return RISC_TCP_OK;
}
inline int32_t failedSetup(Socket& s,int32_t result){
 return destroy(s)==RISC_TCP_OK?result:RISC_TCP_RETAINED;
}
inline int32_t nonblocking(Socket& s){
 if(!checked())return RISC_TCP_RETAINED;
 // lwIP can otherwise wait its default 20 seconds on FIN allocation failure,
 // even on a nonblocking descriptor. Its close path uses this send timeout;
 // timeout expiry is checked by the TCP poll (nominally every 500 ms).
 const timeval timeout{0,1000};
 const int timed=::setsockopt(s.fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
 if(!checked())return RISC_TCP_RETAINED;
 // Without a verified bound, preserve the descriptor; do not attempt close.
 if(timed){retained=true;return RISC_TCP_RETAINED;}
 const int result=::fcntl(s.fd,F_SETFL,O_NONBLOCK);
 if(!checked())return RISC_TCP_RETAINED;
 return result==0?RISC_TCP_OK:failedSetup(s,RISC_TCP_IO);
}
inline int32_t listen(void*,uint64_t owner,const risc_tcp_listen_v1* request,uint64_t* out){
 if(out)*out=0;
 const int32_t allowed=gate();if(allowed)return allowed;
 if(!owner || !request || !out || request->struct_size!=sizeof(*request) || !request->port || request->reserved || request->address[0]>=224)return RISC_TCP_INVALID;
 for(const auto& s:sockets)if(s.token)return RISC_TCP_LIMIT;
 if(generation==UINT64_MAX)return RISC_TCP_LIMIT;
 Operation operation;
 const int32_t online=network();if(online)return online;
 auto& s=sockets[0];
 s.fd=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
 if(s.fd>=0){s.owner=owner;s.token=++generation;s.listener=true;}
 if(!checked())return RISC_TCP_RETAINED;
 if(s.fd<0)return RISC_TCP_IO;
 int32_t result=nonblocking(s);if(result)return result;
 sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(request->port);
 std::memcpy(&address.sin_addr.s_addr,request->address,4);
 if(!checked())return RISC_TCP_RETAINED;
 const int bound=::bind(s.fd,reinterpret_cast<const sockaddr*>(&address),sizeof(address));
 if(!checked())return RISC_TCP_RETAINED;
 if(bound)return failedSetup(s,RISC_TCP_IO);
 const int listening=::listen(s.fd,RISC_TCP_CLIENTS_MAX);
 if(!checked())return RISC_TCP_RETAINED;
 if(listening)return failedSetup(s,RISC_TCP_IO);
 *out=s.token;return RISC_TCP_OK;
}
inline int32_t accept(void*,uint64_t owner,uint64_t listener,uint64_t* out){
 if(out)*out=0;
 const int32_t allowed=gate();if(allowed)return allowed;
 auto* parent=find(owner,listener);
 if(!out || !parent || !parent->listener)return RISC_TCP_CONTEXT;
 Socket* slot=nullptr;for(size_t i=1;i<sizeof(sockets)/sizeof(*sockets);++i)if(!sockets[i].token){slot=&sockets[i];break;}
 if(!slot || generation==UINT64_MAX)return RISC_TCP_LIMIT;
 Operation operation;
 const int32_t online=network();if(online)return online;
 const int fd=::accept(parent->fd,nullptr,nullptr);const int error=errno;
 if(fd>=0){slot->fd=fd;slot->owner=owner;slot->token=++generation;}
 if(!checked())return RISC_TCP_RETAINED;
 if(fd<0)return wouldBlock(error)?RISC_TCP_WOULD_BLOCK:RISC_TCP_IO;
 const int32_t result=nonblocking(*slot);if(result)return result;
 *out=slot->token;return RISC_TCP_OK;
}
inline int32_t transfer(uint64_t owner,uint64_t token,void* output,const void* input,uint32_t size,uint32_t* count){
 if(count)*count=0;
 const int32_t allowed=gate();if(allowed)return allowed;
 auto* s=find(owner,token);if(!s || s->listener)return RISC_TCP_CONTEXT;
 if(!count || (!output && !input) || !size || size>RISC_TCP_BYTES_MAX)return RISC_TCP_INVALID;
 Operation operation;
 const int32_t online=network();if(online)return online;
 const ssize_t result=output?::recv(s->fd,output,size,MSG_DONTWAIT) : ::send(s->fd,input,size,MSG_DONTWAIT);
 const int error=errno;
 if(!checked())return RISC_TCP_RETAINED;
 if(result<0)return wouldBlock(error)?RISC_TCP_WOULD_BLOCK:RISC_TCP_IO;
 if(static_cast<uint64_t>(result)>size){retained=true;return RISC_TCP_RETAINED;}
 if(!result)return output?RISC_TCP_EOF:RISC_TCP_IO;
 *count=static_cast<uint32_t>(result);return RISC_TCP_OK;
}
inline int32_t read(void*,uint64_t owner,uint64_t token,void* bytes,uint32_t size,uint32_t* count){return transfer(owner,token,bytes,nullptr,size,count);}
inline int32_t write(void*,uint64_t owner,uint64_t token,const void* bytes,uint32_t size,uint32_t* count){return transfer(owner,token,nullptr,bytes,size,count);}
inline int32_t close(void*,uint64_t owner,uint64_t token){
 const int32_t allowed=gate();if(allowed)return allowed;
 auto* s=find(owner,token);if(!s)return RISC_TCP_CONTEXT;
 if(s->listener)for(const auto& child:sockets)if(child.token && !child.listener)return RISC_TCP_BUSY;
 Operation operation;return destroy(*s);
}
inline const RiscBoot::TcpListenerBackend* backend(){
 static const RiscBoot::TcpListenerBackend value{nullptr,listen,accept,read,write,close,idle,safe};return &value;
}
} }
