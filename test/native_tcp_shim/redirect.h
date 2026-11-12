#pragma once
// Darwin has no GNU ld --wrap. Redirect only this syscall-shim translation unit,
// after system declarations, preserving the production implementation unchanged.
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
extern "C" {
int __wrap_socket(int,int,int);
int __wrap_setsockopt(int,int,int,const void*,socklen_t);
int __wrap_fcntl(int,int,...);
int __wrap_bind(int,const sockaddr*,socklen_t);
int __wrap_listen(int,int);
int __wrap_accept(int,sockaddr*,socklen_t*);
ssize_t __wrap_recv(int,void*,size_t,int);
ssize_t __wrap_send(int,const void*,size_t,int);
int __wrap_close(int);
int __wrap_connect(int,const sockaddr*,socklen_t);
int __wrap_select(int,fd_set*,fd_set*,fd_set*,timeval*);
int __wrap_getsockopt(int,int,int,void*,socklen_t*);
}
#define socket __wrap_socket
#define setsockopt __wrap_setsockopt
#define fcntl __wrap_fcntl
#define bind __wrap_bind
#define listen __wrap_listen
#define accept __wrap_accept
#define recv __wrap_recv
#define send __wrap_send
#define close __wrap_close
#define connect __wrap_connect
#define select __wrap_select
#define getsockopt __wrap_getsockopt

#define recvfrom __wrap_recvfrom
#define sendto __wrap_sendto
