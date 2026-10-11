#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int mbedtls_sha256_ret(const unsigned char*, size_t, unsigned char[32], int);
#ifdef __cplusplus
}
#endif
