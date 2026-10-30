#include "runtime/dma/DmaReservation.h"
#include <cassert>
#include <cstdio>
using RiscDma::Reservation;
namespace {
bool own=true,failReserve=false,failRelease=false,active=false,failIrq=false,partialIrq=false,failEnable=false,failRetire=false;
unsigned reserveCalls=0,releaseCalls=0,irqCalls=0;bool physical[5]{},irq[5]{};
bool owner(){return own;}
int32_t reserve(size_t n,uint32_t,risc_dma_channel_v1*out){++reserveCalls;physical[n]=true;if(failReserve)return RISC_DMA_PLATFORM;*out={sizeof(*out),1,0,uint32_t(n)};return 0;}
int32_t release(size_t n){++releaseCalls;if(active)return RISC_DMA_BUSY;if(failRelease)return RISC_DMA_PLATFORM;physical[n]=false;return 0;}
int32_t claim(size_t n,risc_dma_irq_handler_v1,void*,bool*out){++irqCalls;*out=!failIrq||partialIrq;irq[n]=*out;return failIrq?RISC_DMA_PLATFORM:0;}
int32_t enable(size_t,bool){return failEnable?RISC_DMA_PLATFORM:0;}
int32_t retire(size_t n){if(failRetire)return RISC_DMA_PLATFORM;irq[n]=false;return 0;}
void callback(void*){}
Reservation::Backend backend{owner,reserve,release,claim,enable,retire};
}
int main(){
 unsigned cases=0;Reservation authority(backend);risc_dma_channel_v1 channel{sizeof(channel),0,0,0};uint64_t token=99;
 assert(authority.reserve(1,0,&token,&channel)==RISC_DMA_INVALID&&token==0&&reserveCalls==0);++cases;
 own=false;assert(authority.reserve(1,RISC_DMA_LCD0,&token,&channel)==RISC_DMA_CONTEXT);own=true;++cases;
 assert(authority.reserve(1,RISC_DMA_LCD0,&token,&channel)==0&&token&&channel.channel==0&&!authority.idle());uint64_t first=token;++cases;
 assert(authority.release(2,token)==RISC_DMA_STALE);own=false;assert(authority.release(1,token)==RISC_DMA_CONTEXT);own=true;++cases;
 active=true;assert(authority.release(1,token)==RISC_DMA_BUSY&&!authority.idle());active=false;++cases;
 assert(authority.irqClaim(1,token,callback,nullptr)==0);unsigned before=releaseCalls;assert(authority.release(1,token)==RISC_DMA_BUSY&&releaseCalls==before);++cases;
 assert(authority.irqClaim(1,token,callback,nullptr)==RISC_DMA_BUSY);assert(authority.irqRetire(2,token)==RISC_DMA_STALE);++cases;
 failEnable=true;assert(authority.irqEnable(1,token,false)==RISC_DMA_PLATFORM);assert(authority.release(1,token)==RISC_DMA_BUSY);failEnable=false;++cases;
 failRetire=true;assert(authority.irqRetire(1,token)==RISC_DMA_PLATFORM);assert(authority.release(1,token)==RISC_DMA_BUSY);failRetire=false;++cases;
 assert(authority.irqRetire(1,token)==0);failRelease=true;assert(authority.release(1,token)==RISC_DMA_PLATFORM&&!authority.idle());failRelease=false;assert(authority.release(1,token)==0&&authority.idle());++cases;
 assert(authority.release(1,first)==RISC_DMA_STALE&&authority.irqRetire(1,first)==RISC_DMA_STALE);++cases;
 failReserve=true;failRelease=true;assert(authority.reserve(1,RISC_DMA_SPI2,&token,&channel)==RISC_DMA_PLATFORM&&token&&!authority.idle()&&channel.channel==UINT32_MAX);assert(authority.irqClaim(1,token,callback,nullptr)==RISC_DMA_INVALID);failReserve=false;failRelease=false;assert(authority.release(1,token)==0);++cases;
 failReserve=true;assert(authority.reserve(1,RISC_DMA_SPI3,&token,&channel)==RISC_DMA_PLATFORM&&!token&&authority.idle());failReserve=false;++cases;
 assert(authority.reserve(1,RISC_DMA_LCD0,&token,&channel)==0);failIrq=true;partialIrq=true;assert(authority.irqClaim(1,token,callback,nullptr)==RISC_DMA_PLATFORM);assert(authority.release(1,token)==RISC_DMA_BUSY);failIrq=partialIrq=false;assert(authority.irqRetire(1,token)==0&&authority.release(1,token)==0);++cases;
 uint64_t tokens[5];for(auto&t:tokens)assert(authority.reserve(1,RISC_DMA_LCD0,&t,&channel)==0);assert(authority.reserve(1,RISC_DMA_LCD0,&token,&channel)==RISC_DMA_CAPACITY&&!token);for(auto t:tokens)assert(authority.release(1,t)==0);++cases;
 assert(authority.reserve(1,RISC_DMA_LCD0,&token,&channel)==0&&token!=first);assert(authority.release(1,first)==RISC_DMA_STALE);assert(authority.release(1,token)==0);++cases;
 std::printf("DMA reservation core: %u cases passed; native SDK/IRQ execution not inferred\n",cases);
}
