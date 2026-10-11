#pragma once
struct _reent { int unused; };
int *__errno(void);
struct _reent *__getreent(void);
extern const char _ctype_[];
