#pragma once
#include <cstddef>
#include <cstdint>
#ifndef RISC_NATIVE_RECOVERY_AXP2101
#define RISC_NATIVE_RECOVERY_AXP2101 0
#endif
namespace RiscCpu { namespace NativeCrownRecovery {
using Transfer=bool(*)(uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t);
#if RISC_NATIVE_RECOVERY_AXP2101
bool opened(uint8_t,uint8_t,uint8_t,Transfer);
bool transact(uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t,Transfer);
bool closing(uint8_t);
bool pause();
void resume(bool gpioWake=false);
uint32_t shortSequence();
bool previousEmergency();
uint32_t previousReason();
void beginInvocation(bool child);
void endInvocation();
bool exitRequested();
#else
inline bool opened(uint8_t,uint8_t,uint8_t,Transfer){return true;}
inline bool transact(uint8_t p,uint8_t a,const uint8_t*t,size_t n,uint8_t*r,size_t z,uint32_t ms,Transfer f){return f(p,a,t,n,r,z,ms);}
inline bool closing(uint8_t){return true;}
inline bool pause(){return true;}
inline void resume(bool=false){}
inline uint32_t shortSequence(){return 0;}
inline bool previousEmergency(){return false;}
inline uint32_t previousReason(){return 0;}
inline void beginInvocation(bool){}
inline void endInvocation(){}
inline bool exitRequested(){return false;}
#endif
}}
