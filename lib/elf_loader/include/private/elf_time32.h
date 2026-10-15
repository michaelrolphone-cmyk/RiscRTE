#pragma once
/* Frozen libc export ABI for previously admitted ESP-IDF4 ELFs. The SDK6
 * native timespec is 16 bytes; passing an old 8-byte object to it corrupts the
 * caller. New native code uses clock_gettime directly, outside this ELF table. */
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <stddef.h>
struct risc_elf_timespec32 {int32_t tv_sec;int32_t tv_nsec;};
#if defined(__cplusplus)
static_assert(sizeof(struct risc_elf_timespec32)==8 && offsetof(struct risc_elf_timespec32,tv_nsec)==4,"time32 ELF ABI");
#else
_Static_assert(sizeof(struct risc_elf_timespec32)==8 && offsetof(struct risc_elf_timespec32,tv_nsec)==4,"time32 ELF ABI");
#endif
static inline int risc_elf_time32_copy(const struct timespec* native,struct risc_elf_timespec32* out){
    if(!native || !out){errno=EINVAL;return -1;}
    if(native->tv_sec<INT32_MIN || native->tv_sec>INT32_MAX || native->tv_nsec<0 || native->tv_nsec>=1000000000L){
        errno=EOVERFLOW;return -1;
    }
    const struct risc_elf_timespec32 converted={(int32_t)native->tv_sec,(int32_t)native->tv_nsec};
    *out=converted;return 0;
}
static inline int risc_elf_clock_gettime32(clockid_t clock,struct risc_elf_timespec32* out){
    if(!out){errno=EINVAL;return -1;}
    struct timespec native;
    if(clock_gettime(clock,&native)!=0)return -1;
    return risc_elf_time32_copy(&native,out);
}
