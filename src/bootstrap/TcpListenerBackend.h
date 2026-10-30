#pragma once
#include <RiscTcpListenerV1.h>
namespace RiscBoot {
// Native-only backend. The broker supplies a unique provider activation key;
// no manifest, provider argument or ELF import can choose this identity.
struct TcpListenerBackend {
 void* context;
 int32_t (*listen)(void*,uint64_t,const risc_tcp_listen_v1*,uint64_t*);
 int32_t (*accept)(void*,uint64_t,uint64_t,uint64_t*);
 int32_t (*read)(void*,uint64_t,uint64_t,void*,uint32_t,uint32_t*);
 int32_t (*write)(void*,uint64_t,uint64_t,const void*,uint32_t,uint32_t*);
 int32_t (*close)(void*,uint64_t,uint64_t);
 bool (*idle)(void*,uint64_t); // zero queries all owners; no I/O
 bool (*safe)(void*);          // false after uncertain cleanup/context loss
 int32_t (*connect)(void*,uint64_t,const risc_tcp_listen_v1*,uint64_t*)=nullptr;
 int32_t (*connected)(void*,uint64_t,uint64_t)=nullptr;
 int32_t (*datagram_bind)(void*,uint64_t,const risc_tcp_listen_v1*,uint64_t*)=nullptr;
 int32_t (*datagram_receive)(void*,uint64_t,uint64_t,void*,uint32_t,uint32_t*,risc_tcp_listen_v1*)=nullptr;
 int32_t (*datagram_send)(void*,uint64_t,uint64_t,const void*,uint32_t,const risc_tcp_listen_v1*)=nullptr;
};
}
