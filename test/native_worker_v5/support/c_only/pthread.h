#pragma once
#include <stdlib.h>
#include <assert.h>
/* Used ONLY for the three C TUs, never in host C++/std::thread headers. */
typedef unsigned long pthread_key_t;
int pthread_key_create(pthread_key_t *, void (*)(void *));
int pthread_key_delete(pthread_key_t);
void *pthread_getspecific(pthread_key_t);
int pthread_setspecific(pthread_key_t, const void *);
