#include "runtime/control/CrownRecovery.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
using RiscControl::CrownRecovery;
static void quiet(CrownRecovery& key,uint32_t begin,uint32_t end){
  for(uint32_t n=begin;n<end;n+=10)assert(!key.sample(0,n));
}
int main(){
  {CrownRecovery k;quiet(k,0,10000);assert(!k.held()&&!k.fired());}
  {CrownRecovery k;assert(!k.sample(2,0));quiet(k,10,5000);assert(k.sample(0,5000));
   assert(k.fired());for(unsigned i=0;i<1000;i++)assert(!k.sample(0,5010+i));}
  for(uint32_t release:{10u,29u,30u,1000u,2000u,4990u,5000u}){
    CrownRecovery k;assert(!k.sample(2,0));quiet(k,10,release);
    assert(!k.sample(1,release));quiet(k,release+10,release+6000);assert(!k.fired());
  }
  // PMIC long status is not itself the emergency threshold.
  {CrownRecovery k;assert(!k.sample(4,1000));quiet(k,1010,6000);assert(k.sample(0,6000));}
  // Bounce and coalesced opposite edges must never accumulate old hold time.
  {CrownRecovery k;assert(!k.sample(2,0));assert(!k.sample(1,20));assert(!k.sample(2,40));
   quiet(k,50,5040);assert(k.sample(0,5040));}
  {CrownRecovery k;assert(!k.sample(3,0));quiet(k,10,6000);assert(!k.held());}
  // Lost bus access or a scheduler/sleep gap breaks continuous evidence.
  {CrownRecovery k;assert(!k.sample(2,0));quiet(k,10,4990);assert(!k.sample(0,4990,false));quiet(k,5000,11000);assert(!k.fired());}
  {CrownRecovery k;assert(!k.sample(2,0));quiet(k,10,1000);assert(!k.sample(0,6000));quiet(k,6010,12000);assert(!k.fired());}
  {CrownRecovery k;uint32_t begin=UINT32_MAX-2500;assert(!k.sample(2,begin));
   for(uint32_t d=10;d<5000;d+=10)assert(!k.sample(0,begin+d));assert(k.sample(0,begin+5000));}
  {RiscControl::CrownEvents e;assert(!e.observe(8,0));assert(e.shortSequence()==1 && e.legacy()==8);
   e.acknowledge(15);assert(!e.legacy()&&e.shortSequence()==1);assert(!e.observe(2,10));
   e.acknowledge(15);for(unsigned n=20;n<5010;n+=10)assert(!e.observe(0,n));assert(e.observe(0,5010));}
  {RiscControl::CrownEvents e;e.waking();assert(!e.observe(2,0));assert(!e.observe(9,100));
   assert(e.shortSequence()==0 && !(e.legacy()&8));e.acknowledge(15);
   assert(!e.observe(2,200));assert(!e.observe(9,300));assert(e.shortSequence()==1);}
  {RiscControl::CrownEvents e;e.waking();assert(!e.observe(2,0));
   for(unsigned n=10;n<5000;n+=10)assert(!e.observe(0,n));assert(e.observe(0,5000));}
  {RiscControl::CrownExit x;x.begin(false);x.press(10);assert(!x.pending()&&!x.expired(9999));
   x.begin(true);x.press(100);assert(x.pending()&&!x.expired(3099));x.press(2000);
   assert(x.expired(3100));x.end();assert(!x.pending());
   x.begin(true);x.press(UINT32_MAX-1000);assert(x.expired(1999));}
  puts("Crown policy: threshold, release, bounce, ambiguity, boot/wake gaps, wrap, independent consumers and single reset PASS");
}
