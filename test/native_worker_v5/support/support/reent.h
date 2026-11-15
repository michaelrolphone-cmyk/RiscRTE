#pragma once
#include <errno.h>
struct _reent { int unused; };
static inline int *__errno(void){return &errno;}
static inline struct _reent *__getreent(void){return 0;}
static const char _ctype_[384]={0};
