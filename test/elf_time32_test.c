#include "private/elf_time32.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
 struct {uint32_t head;struct risc_elf_timespec32 value;uint32_t tail;} buffer={0x11223344,{17,23},0xaabbccdd};
 struct timespec native={INT32_MAX,999999999};
 assert(risc_elf_time32_copy(&native,&buffer.value)==0);
 assert(buffer.value.tv_sec==INT32_MAX && buffer.value.tv_nsec==999999999);
 native.tv_sec=INT32_MIN;native.tv_nsec=0;assert(risc_elf_time32_copy(&native,&buffer.value)==0);
 struct risc_elf_timespec32 saved=buffer.value;
 native.tv_sec=(int64_t)INT32_MAX+1;assert(risc_elf_time32_copy(&native,&buffer.value)==-1 && errno==EOVERFLOW);
 assert(memcmp(&saved,&buffer.value,sizeof saved)==0);
 native.tv_sec=(int64_t)INT32_MIN-1;assert(risc_elf_time32_copy(&native,&buffer.value)==-1);
 native.tv_sec=0;native.tv_nsec=-1;assert(risc_elf_time32_copy(&native,&buffer.value)==-1);
 native.tv_nsec=1000000000;assert(risc_elf_time32_copy(&native,&buffer.value)==-1);
 assert(risc_elf_clock_gettime32((clockid_t)-123,&buffer.value)==-1 && memcmp(&saved,&buffer.value,sizeof saved)==0);
 assert(risc_elf_clock_gettime32(CLOCK_MONOTONIC,&buffer.value)==0);
 assert(buffer.value.tv_sec>=0 && buffer.value.tv_nsec>=0 && buffer.value.tv_nsec<1000000000);
 assert(risc_elf_clock_gettime32(CLOCK_MONOTONIC,NULL)==-1 && errno==EINVAL);
 assert(buffer.head==0x11223344 && buffer.tail==0xaabbccdd);
 puts("Frozen time32 ELF export: guard bytes, signed boundaries, overflow, invalid nanos/clock and real monotonic translation PASS");
}
