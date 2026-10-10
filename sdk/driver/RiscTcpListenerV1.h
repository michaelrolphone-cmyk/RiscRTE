#pragma once
/* Provider-only, explicitly selected nonblocking IPv4 TCP transport. No
 * credentials, discovery, protocol parsing, filesystem or radio control.
 * Buffers are borrowed only for the duration of a call. Close every client,
 * then the listener, before releasing the provider or its Wi-Fi dependency. */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_TCP_LISTENER_CAPABILITY "platform.tcp-listener"
#define RISC_TCP_LISTENER_API_V1 1u
#define RISC_TCP_LISTENERS_MAX 1u
#define RISC_TCP_CLIENTS_MAX 4u
#define RISC_TCP_BYTES_MAX 2048u
enum {
 RISC_TCP_OK=0, RISC_TCP_WOULD_BLOCK=1, RISC_TCP_EOF=2,
 RISC_TCP_INVALID=-1, RISC_TCP_CONTEXT=-2, RISC_TCP_LIMIT=-3,
 RISC_TCP_NETWORK_DOWN=-4, RISC_TCP_IO=-5, RISC_TCP_RETAINED=-6,
 RISC_TCP_BUSY=-7
};
typedef struct {
 uint32_t struct_size;
 uint8_t address[4]; /* Explicit network-order IPv4 octets; 0.0.0.0 means any. */
 uint16_t port;      /* Host byte order, 1..65535. No implicit/ephemeral port. */
 uint16_t reserved;  /* Must be zero. */
} risc_tcp_listen_v1;
typedef struct {
 uint32_t api_version,struct_size;
 void* context; /* Opaque provider activation generation; never interchangeable. */
 int32_t (*listen)(void*,const risc_tcp_listen_v1*,uint64_t*);
 int32_t (*accept)(void*,uint64_t,uint64_t*);
 /* One bounded operation. OK reports 1..capacity bytes; partial writes are
  * normal. WOULD_BLOCK and EOF report zero. EOF is read-only. */
 int32_t (*read)(void*,uint64_t,void*,uint32_t,uint32_t*);
 int32_t (*write)(void*,uint64_t,const void*,uint32_t,uint32_t*);
 /* Closes either kind of handle. A listener with clients returns BUSY.
  * Failed/uncertain close returns RETAINED; the handle must not be reused. */
 int32_t (*close)(void*,uint64_t);
} risc_tcp_listener_v1;
#ifdef __cplusplus
}
#endif
