#include "ports/esp32s3/NativeSettingsStartupEntropy.cpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <set>
namespace E=RiscCpu::NativeSettingsStartupEntropy;
static bool ownerNow=true,isr=false,inside=false,zero=false,lose=false,hashFail=false,allow=true,reenter=false,external=false;
static unsigned enables=0,disables=0,fills=0,authCalls=0;
bool esp_ptr_internal(const void*p){return p&&!external;}
extern "C" int xPortInIsrContext(){return isr;}
static bool owner(){return ownerNow;}
extern "C" void bootloader_random_enable(){assert(!inside);inside=true;++enables;}
extern "C" void bootloader_random_disable(){assert(inside);inside=false;++disables;}
extern "C" void esp_fill_random(void*p,size_t n){assert(inside&&n==32);++fills;memset(p,zero?0:0x37,n);if(lose)ownerNow=false;}
bool entropy_test_hash_fail(){return hashFail;}
static bool auth(void*){++authCalls;if(reenter){reenter=false;uint8_t out[16]{};assert(!E::take(1,E::Purpose::Incarnation,nullptr,auth,out));}return allow;}
static void reset(){E::wipe(E::state.seed,32);E::state.busy.store(0);E::state.counter=0;E::state.attempted=E::state.ready=false;ownerNow=allow=true;isr=inside=zero=lose=hashFail=reenter=external=false;enables=disables=fills=authCalls=0;}
static void unchanged(const uint8_t*p){for(unsigned i=0;i<16;++i)assert(p[i]==0x91);}
int main(){
 uint8_t out[16];memset(out,0x91,16);assert(!E::take(1,E::Purpose::Incarnation,nullptr,auth,out));unchanged(out);
 assert(E::capture(owner)&&enables==1&&disables==1&&fills==1&&!inside);assert(!E::capture(owner)&&fills==1);
 std::set<std::string> ids;for(unsigned i=0;i<1000;++i){assert(E::take(i%2?1:6,i%3?E::Purpose::Incarnation:E::Purpose::FirstCheckpoint,nullptr,auth,out));assert(ids.insert(std::string(reinterpret_cast<char*>(out),16)).second);}assert(fills==1);
 reenter=true;assert(E::take(1,E::Purpose::FirstCheckpoint,nullptr,auth,out));assert(!reenter);assert(ids.insert(std::string(reinterpret_cast<char*>(out),16)).second);
 memset(out,0x91,16);allow=false;const auto serial=E::state.counter;assert(!E::take(1,E::Purpose::Incarnation,nullptr,auth,out)&&E::state.counter==serial);unchanged(out);allow=true;
 assert(!E::take(7,E::Purpose::Incarnation,nullptr,auth,out));assert(!E::take(1,E::Purpose(0),nullptr,auth,out));assert(!E::take(1,E::Purpose::Incarnation,nullptr,nullptr,out));unchanged(out);
 isr=true;assert(!E::take(1,E::Purpose::Incarnation,nullptr,auth,out));assert(!E::capture(owner));isr=false;unchanged(out);
 hashFail=true;assert(!E::take(1,E::Purpose::Incarnation,nullptr,auth,out)&&E::state.counter==serial+1);hashFail=false;unchanged(out);
 unsigned remaining=1;auto lost=[](void*p){return --*static_cast<unsigned*>(p)!=0;};remaining=2;assert(!E::take(1,E::Purpose::Incarnation,&remaining,lost,out));unchanged(out);
 E::state.counter=UINT32_MAX;assert(!E::take(1,E::Purpose::Incarnation,nullptr,auth,out)&&E::state.counter==UINT32_MAX);unchanged(out);
 reset();zero=true;assert(!E::capture(owner)&&fills==1&&enables==disables&&!inside&&!E::state.ready);assert(!E::capture(owner));
 reset();lose=true;assert(!E::capture(owner)&&fills==1&&enables==disables&&!inside&&!E::state.ready);
 reset();ownerNow=false;assert(!E::capture(owner)&&!fills&&!enables);
 reset();external=true;assert(!E::capture(owner)&&!fills&&!enables);
 reset();isr=true;assert(!E::capture(owner)&&!E::state.attempted&&!fills);isr=false;assert(E::capture(owner));
 printf("PASS startup entropy: paired early hardware window, 1001 unique IDs, custody/ISR/reentry/hash/counter/zero-source refusals; host controls=%zu\n",E::controlBytes());
}
