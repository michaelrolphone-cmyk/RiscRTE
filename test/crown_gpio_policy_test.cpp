#include "runtime/control/CrownGpio.h"
#include "runtime/control/CrownRecovery.h"
#include <cassert>
#include <cstdio>
int main(){
  RiscControl::CrownGpio gpio;RiscControl::CrownEvents events;
  auto sample=[&](bool down,uint32_t at){return events.observe(gpio.sample(down,at),at);};
  for(unsigned t=0;t<=100;t+=10)assert(!sample(false,t));
  assert(!sample(true,110));assert(!sample(false,120));assert(!sample(true,130));
  for(unsigned t=140;t<=200;t+=10)assert(!sample(true,t));
  for(unsigned t=210;t<=240;t+=10)assert(!sample(false,t));
  assert(events.shortSequence()==1);
  gpio.reset();events=RiscControl::CrownEvents{};
  for(unsigned t=0;t<=100;t+=10)assert(!sample(true,t));
  for(unsigned t=110;t<=140;t+=10)assert(!sample(false,t));
  assert(events.shortSequence()==0); // Boot-held release is not navigation.
  gpio.reset();events=RiscControl::CrownEvents{};
  for(unsigned t=0;t<5020;t+=10)assert(!sample(true,t));
  assert(sample(true,5020));
  gpio.reset();events=RiscControl::CrownEvents{};
  for(unsigned t=0;t<=5000;t+=10)assert(!sample(true,t));
  for(unsigned t=5010;t<=5050;t+=10)assert(!sample(false,t));
  gpio.reset();events=RiscControl::CrownEvents{};
  for(unsigned t=0;t<=4900;t+=10)assert(!sample(true,t));
  assert(!sample(true,5300)); // Missing samples break the old hold.
  for(unsigned t=5310;t<10320;t+=10)assert(!sample(true,t));
  assert(sample(true,10320));
  puts("GPIO crown debounce, boot release, five-second hold and sample gap PASS");
}
