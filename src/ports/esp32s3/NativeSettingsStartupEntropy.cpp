#include "NativeSettingsStartupEntropy.h"
#if RISC_NATIVE_SETTINGS_STARTUP_ENTROPY
#include "NativeInternalAtomics.h"
#include <cstring>
#include <bootloader_random.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "runtime/crypto/Sha256.h"
#include "NativeMemory.h"
namespace RiscCpu { namespace NativeSettingsStartupEntropy { namespace {
struct State {std::atomic<uint32_t>busy{0};uint8_t seed[32]{};uint32_t counter=0;bool attempted=false,ready=false;};
static RISC_NATIVE_ATOMIC_DRAM State state;
void wipe(void*p,size_t n){volatile uint8_t*b=static_cast<volatile uint8_t*>(p);while(n--)*b++=0;}
bool internal(const void*p,size_t n){return p&&n&&uintptr_t(p)<=UINTPTR_MAX-(n-1)&&esp_ptr_internal(p)&&esp_ptr_internal(static_cast<const uint8_t*>(p)+n-1);}
struct Lock {bool held=false;Lock(){uint32_t expected=0;held=state.busy.compare_exchange_strong(expected,1,std::memory_order_acquire);}~Lock(){if(held)state.busy.store(0,std::memory_order_release);}};
struct Scratch {uint8_t input[64]{},digest[32]{};~Scratch(){wipe(this,sizeof *this);}};
bool nonzero(const uint8_t*p,size_t n){uint8_t v=0;while(n--)v|=*p++;return v!=0;}
void word(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(i*8));}
} // namespace
bool capture(bool(*owner)()){
 if(xPortInIsrContext() || !NativeInternalAtomics::internal(state))return false;
 Lock lock;
 if(!lock.held||state.attempted)return false;
 state.attempted=true;
 if(!owner||!owner()||!internal(&state,sizeof state))return false;
 Scratch scratch;if(!internal(&scratch,sizeof scratch))return false;
 // This is the sole SAR entropy window. Always restore the peripheral state
 // before checking output/custody or allowing any subsequent initialization.
 bootloader_random_enable();esp_fill_random(scratch.digest,sizeof scratch.digest);bootloader_random_disable();
 if(!owner()||!nonzero(scratch.digest,sizeof scratch.digest))return false;
 std::memcpy(state.seed,scratch.digest,sizeof state.seed);state.ready=true;return true;
}
bool take(uint32_t ns,Purpose purpose,void*context,bool(*authorized)(void*),uint8_t out[16]){
 if(xPortInIsrContext() || !NativeInternalAtomics::internal(state))return false;
 Lock lock;
 if(!lock.held||!state.ready||!out||(ns!=1&&ns!=6)||!authorized||
    (purpose!=Purpose::Incarnation&&purpose!=Purpose::FirstCheckpoint)||!authorized(context))return false;
 if(state.counter==UINT32_MAX)return false;
 Scratch scratch;if(!internal(&scratch,sizeof scratch))return false;
 // Fixed domain, independent namespace/purpose and globally unique boot-local
 // serial. The seed is never an ID, caller-supplied value, log or persistent key.
 constexpr uint8_t domain[16]={'R','T','E','-','S','E','T','T','I','N','G','-','I','D',1,0};
 std::memcpy(scratch.input,domain,sizeof domain);std::memcpy(scratch.input+16,state.seed,sizeof state.seed);
 word(scratch.input+48,ns);word(scratch.input+52,uint32_t(purpose));word(scratch.input+56,++state.counter);
 RiscCrypto::Sha256 hash;hash.initialize();
 bool ok=hash.begin();
 if(ok)ok=hash.add(scratch.input,sizeof scratch.input);
 if(ok)ok=hash.finish(scratch.digest);
 hash.release();
 if(!ok||!authorized(context)||!nonzero(scratch.digest,16))return false;
 std::memcpy(out,scratch.digest,16);return true;
}
size_t controlBytes(){return sizeof state;}
} }
#endif
