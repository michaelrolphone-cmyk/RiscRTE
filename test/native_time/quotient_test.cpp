#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include "NativeTime.h"
#include "NativeTimeQuotient.h"
static int64_t timer;
static unsigned timer_calls;
[[maybe_unused]] static uint32_t arduino_value;
#if RISC_NATIVE_ESP_IDF
extern "C" int64_t esp_timer_get_time(){++timer_calls;return timer;}
#else
unsigned long millis(){++timer_calls;return arduino_value;}
#endif
static uint32_t current(uint64_t n){return uint32_t(n/1000u);}
static uint32_t limbs(uint64_t n){return RiscPlatform::unsignedMicrosToMillis32(n);}
static uint64_t count;
static uint32_t target_div32(uint32_t n){return uint32_t((uint64_t(n)*UINT64_C(0x10624dd3))>>38);}
static void check(uint64_t n){uint32_t expected=uint32_t(n/1000u);assert(current(n)==expected);assert(limbs(n)==expected);assert(target_div32(uint32_t(n>>32))==uint32_t(n>>32)/1000u);++count;}
int main(){
 for(uint64_t n=0;n<2000000;n++)check(n);
 for(unsigned bit=0;bit<64;bit++){uint64_t base=UINT64_C(1)<<bit;for(int d=-2000;d<=2000;d++)check(base+uint64_t(d));}
 const uint64_t qs[]={0,1,65535,65536,UINT32_MAX-1,UINT32_MAX,UINT64_C(1)<<32,(UINT64_C(1)<<32)+1,UINT64_MAX/1000};
 for(uint64_t q:qs){uint64_t base=q*1000;for(int d=-1001;d<=1001;d++)check(base+uint64_t(d));}
 // Every possible incoming remainder and 16-bit next limb: all 65,536,000
 // recurrence inputs; validates both intermediate bound and quotient digit.
 for(uint32_t rem=0;rem<1000;rem++)for(uint32_t limb=0;limb<65536;limb++){
  uint32_t v=(rem<<16)|limb,q=v/1000,r=v-q*1000;
  assert(q<65536&&r<1000&&uint64_t(q)*1000+r==v);assert(target_div32(v)==q);
 }
 uint64_t rng=UINT64_C(0x6a09e667f3bcc909);
 for(unsigned i=0;i<10000000;i++){rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;check(rng);}
 const int64_t signedcases[]={INT64_MIN,INT64_MIN+1,-1001,-1000,-999,-1,0,1,999,1000,1001,INT64_MAX-1,INT64_MAX};
 for(int64_t t:signedcases){timer=t;check(uint64_t(t));timer_calls=0;
#if RISC_NATIVE_ESP_IDF
 assert(RiscPlatform::millis()==uint32_t(uint64_t(t)/1000u));
#else
 arduino_value=uint32_t(uint64_t(t));assert(RiscPlatform::millis()==arduino_value);
#endif
 assert(timer_calls==1);}
 printf("PASS full64_comparisons=%llu exhaustive_limb_recurrences=65536000 signed_timer_cases=13\n",(unsigned long long)count);
}
