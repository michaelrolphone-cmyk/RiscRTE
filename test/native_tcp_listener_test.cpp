#include "ports/esp32s3/NativeTcpListener.h"
#include <cassert>
#include <cstdio>
#include <cstdarg>
using namespace RiscCpu;
static bool owns=true,online=true,networkLose=false;
static int calls=0,nextFd=10,failCall=0,loseCall=0,accepted=10,readResult=2,writeResult=3,closeResult=0;
static sockaddr_in bound{};
static bool owner(){return owns;}
static bool network(){if(networkLose)owns=false;return online;}
static int step(){++calls;if(calls==loseCall)owns=false;if(calls==failCall){errno=EIO;return -1;}return 0;}
extern "C" int __wrap_socket(int domain,int type,int protocol){assert(domain==AF_INET&&type==SOCK_STREAM&&protocol==IPPROTO_TCP);return step()<0?-1:nextFd++;}
extern "C" int __wrap_setsockopt(int,int level,int name,const void* data,socklen_t size){assert(level==SOL_SOCKET&&name==SO_SNDTIMEO&&size==sizeof(timeval));const auto* t=static_cast<const timeval*>(data);assert(t->tv_sec==0&&t->tv_usec==1000);return step();}
extern "C" int __wrap_fcntl(int,int command,...){assert(command==F_SETFL);va_list args;va_start(args,command);assert(va_arg(args,int)==O_NONBLOCK);va_end(args);return step();}
extern "C" int __wrap_bind(int,const sockaddr* address,socklen_t size){assert(size==sizeof(bound));memcpy(&bound,address,size);return step();}
extern "C" int __wrap_listen(int,int backlog){assert(backlog==4);return step();}
extern "C" int __wrap_accept(int,sockaddr*,socklen_t*){if(step()<0)return -1;if(accepted<0){errno=EAGAIN;return -1;}return nextFd++;}
extern "C" ssize_t __wrap_recv(int,void* bytes,size_t size,int flags){assert(flags==MSG_DONTWAIT);if(step()<0)return -1;if(readResult<0){errno=EWOULDBLOCK;return -1;}assert(unsigned(readResult)<=size);memset(bytes,'r',readResult);return readResult;}
extern "C" ssize_t __wrap_send(int,const void*,size_t size,int flags){assert(flags==MSG_DONTWAIT);if(step()<0)return -1;if(writeResult<0){errno=EAGAIN;return -1;}assert(unsigned(writeResult)<=size);return writeResult;}
extern "C" int __wrap_close(int){if(step()<0)return -1;return closeResult;}
static const auto* api=NativeTcpListener::backend();
static void reset(){
 owns=online=true;networkLose=false;calls=failCall=loseCall=closeResult=0;accepted=10;readResult=2;writeResult=3;
 for(auto& s:NativeTcpListener::sockets)s=NativeTcpListener::Socket{};
 NativeTcpListener::retained=NativeTcpListener::busy=false;
 NativeTcpListener::configure(owner,network);
}
static risc_tcp_listen_v1 request{sizeof(request),{192,168,2,7},8080,0};
static uint64_t listen(){uint64_t t=0;assert(api->listen(nullptr,71,&request,&t)==0&&t);return t;}
static uint64_t accept(uint64_t t){uint64_t c=0;assert(api->accept(nullptr,71,t,&c)==0&&c);return c;}
static void noIo(uint64_t l,uint64_t c,int32_t result){
 int old=calls;char bytes[8]{};uint32_t n=99;uint64_t t=99;
 assert(api->listen(nullptr,71,&request,&t)==result&&t==0);
 assert(api->accept(nullptr,71,l,&t)==result&&t==0);
 assert(api->read(nullptr,71,c,bytes,sizeof(bytes),&n)==result&&n==0);
 assert(api->write(nullptr,71,c,bytes,sizeof(bytes),&n)==result&&n==0);
 assert(api->close(nullptr,71,c)==result&&calls==old);
}
int main(){
 reset();auto l=listen();assert(bound.sin_port==htons(8080)&&!memcmp(&bound.sin_addr.s_addr,request.address,4));
 auto c=accept(l);char bytes[8]{};uint32_t n=99;uint64_t other=99;int before=calls;
 assert(api->read(nullptr,72,c,bytes,8,&n)==RISC_TCP_CONTEXT&&n==0);
 assert(api->close(nullptr,72,c)==RISC_TCP_CONTEXT&&calls==before);
 assert(api->accept(nullptr,72,l,&other)==RISC_TCP_CONTEXT&&other==0&&calls==before);
 assert(api->read(nullptr,71,c,bytes,RISC_TCP_BYTES_MAX+1,&n)==RISC_TCP_INVALID&&calls==before);
 assert(api->read(nullptr,71,c,bytes,8,&n)==0&&n==2);
 assert(api->write(nullptr,71,c,bytes,8,&n)==0&&n==3);
 readResult=writeResult=-1;
 assert(api->read(nullptr,71,c,bytes,8,&n)==RISC_TCP_WOULD_BLOCK&&n==0);
 assert(api->write(nullptr,71,c,bytes,8,&n)==RISC_TCP_WOULD_BLOCK&&n==0);
 readResult=writeResult=0;
 assert(api->read(nullptr,71,c,bytes,8,&n)==RISC_TCP_EOF&&n==0);
 assert(api->write(nullptr,71,c,bytes,8,&n)==RISC_TCP_IO&&n==0);
 accepted=-1;assert(api->accept(nullptr,71,l,&other)==RISC_TCP_WOULD_BLOCK&&other==0);accepted=10;
 assert(api->close(nullptr,71,l)==RISC_TCP_BUSY);
 owns=false;noIo(l,c,RISC_TCP_CONTEXT);owns=true;
 online=false;before=calls;
 assert(api->read(nullptr,71,c,bytes,8,&n)==RISC_TCP_NETWORK_DOWN&&calls==before);
 assert(api->accept(nullptr,71,l,&other)==RISC_TCP_NETWORK_DOWN&&calls==before);
 assert(api->close(nullptr,71,c)==0);online=true;
 auto newer=accept(l);assert(newer!=c);before=calls;
 assert(api->read(nullptr,71,c,bytes,8,&n)==RISC_TCP_CONTEXT&&calls==before);
 uint64_t clients[4]{newer,accept(l),accept(l),accept(l)};before=calls;
 assert(api->accept(nullptr,71,l,&other)==RISC_TCP_LIMIT&&calls==before);
 assert(api->listen(nullptr,72,&request,&other)==RISC_TCP_LIMIT&&calls==before);
 for(auto client:clients)assert(api->close(nullptr,71,client)==0);
 assert(api->close(nullptr,71,l)==0&&api->idle(nullptr,0));
 // Every setup failure disposes of an acquired descriptor or retains custody.
 for(int fail=1;fail<=5;++fail){reset();failCall=fail;other=99;assert(api->listen(nullptr,71,&request,&other)==(fail==2?RISC_TCP_RETAINED:RISC_TCP_IO)&&other==0);assert(api->idle(nullptr,0)==(fail!=2));}
 reset();failCall=3;closeResult=-1;other=99;
 assert(api->listen(nullptr,71,&request,&other)==RISC_TCP_RETAINED&&other==0&&!api->idle(nullptr,0));
 noIo(1,2,RISC_TCP_RETAINED);
 reset();l=listen();c=accept(l);closeResult=-1;
 assert(api->close(nullptr,71,c)==RISC_TCP_RETAINED&&!api->safe(nullptr));
 noIo(l,c,RISC_TCP_RETAINED);
 // Owner loss at each step must prevent the very next native syscall.
 for(int loss=1;loss<=5;++loss){reset();loseCall=loss;other=99;assert(api->listen(nullptr,71,&request,&other)==RISC_TCP_RETAINED&&calls==loss&&other==0);owns=true;noIo(1,2,RISC_TCP_RETAINED);}
 for(int loss=1;loss<=3;++loss){reset();l=listen();loseCall=calls+loss;assert(api->accept(nullptr,71,l,&other)==RISC_TCP_RETAINED&&calls==loseCall&&other==0);owns=true;noIo(l,1,RISC_TCP_RETAINED);}
 reset();l=listen();c=accept(l);loseCall=calls+1;
 assert(api->read(nullptr,71,c,bytes,8,&n)==RISC_TCP_RETAINED&&n==0);owns=true;noIo(l,c,RISC_TCP_RETAINED);
 reset();l=listen();c=accept(l);loseCall=calls+1;
 assert(api->close(nullptr,71,c)==RISC_TCP_RETAINED);owns=true;noIo(l,c,RISC_TCP_RETAINED);
 reset();l=listen();c=accept(l);loseCall=calls+1;
 assert(api->write(nullptr,71,c,bytes,8,&n)==RISC_TCP_RETAINED&&n==0);owns=true;noIo(l,c,RISC_TCP_RETAINED);
 reset();networkLose=true;other=99;assert(api->listen(nullptr,71,&request,&other)==RISC_TCP_RETAINED&&calls==0&&other==0);
 reset();l=listen();failCall=calls+3;assert(api->accept(nullptr,71,l,&other)==RISC_TCP_IO&&other==0&&api->safe(nullptr));assert(api->close(nullptr,71,l)==0);
 reset();l=listen();failCall=calls+2;assert(api->accept(nullptr,71,l,&other)==RISC_TCP_RETAINED&&other==0);noIo(l,1,RISC_TCP_RETAINED);
 reset();auto invalid=request;invalid.port=0;assert(api->listen(nullptr,71,&invalid,&other)==RISC_TCP_INVALID&&calls==0);
 invalid=request;invalid.reserved=1;assert(api->listen(nullptr,71,&invalid,&other)==RISC_TCP_INVALID&&calls==0);
 invalid=request;invalid.address[0]=224;assert(api->listen(nullptr,71,&invalid,&other)==RISC_TCP_INVALID&&calls==0);
 reset();online=false;other=99;assert(api->listen(nullptr,71,&request,&other)==RISC_TCP_NETWORK_DOWN&&calls==0&&other==0);
 puts("Native TCP production syscall shim: bounds, partial/blocked/EOF, owner generations, Wi-Fi, retained close and no I/O after context loss PASS");
}
