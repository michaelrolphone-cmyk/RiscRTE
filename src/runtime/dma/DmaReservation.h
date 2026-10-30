#pragma once
#include <RiscDmaReservationV1.h>
#include <cstddef>
namespace RiscDma {
/* Owner-serialized resident descriptors; physical allocation remains the SDK's.
 * Backend release must retain all uncertain resources on failure. No callback
 * runs through this object; IRQ backend owns its callback until actual retirement. */
class Reservation final {
 public:
 static constexpr size_t kSlots=5;
 struct Backend {
  bool (*owner)();
  int32_t (*reserve)(size_t,uint32_t,risc_dma_channel_v1*);
  int32_t (*release)(size_t);
  int32_t (*irqClaim)(size_t,risc_dma_irq_handler_v1,void*,bool*);
  int32_t (*irqEnable)(size_t,bool);
  int32_t (*irqRetire)(size_t);
 };
 explicit Reservation(Backend b):backend_(b){}
 Reservation(const Reservation&)=delete;Reservation&operator=(const Reservation&)=delete;
 bool owner()const{return backend_.owner&&backend_.owner();}
 bool idle()const{for(const auto&s:slots_)if(s.token)return false;return true;}
 int32_t reserve(uint64_t scope,uint32_t route,uint64_t*out,risc_dma_channel_v1*channel){
  if(out)*out=0;
  if(!owner())return RISC_DMA_CONTEXT;
  if(!scope||!out||!channel||channel->struct_size<sizeof(*channel)||route<RISC_DMA_SPI2||route>RISC_DMA_RMT0||!backend_.reserve||!backend_.release)return RISC_DMA_INVALID;
  channel->backend=channel->group=channel->channel=UINT32_MAX;
  if(serial_==UINT64_MAX)return RISC_DMA_CAPACITY;
  size_t n=0;while(n<kSlots&&slots_[n].token)++n;if(n==kSlots)return RISC_DMA_CAPACITY;
  auto&s=slots_[n];s={++serial_,scope,false,false};*out=s.token;
  risc_dma_channel_v1 result{sizeof(result),UINT32_MAX,UINT32_MAX,UINT32_MAX};
  const int32_t status=backend_.reserve(n,route,&result);
  if(status){if(backend_.release(n)==RISC_DMA_OK){s={};*out=0;}return status;}
  s.ready=true;*channel=result;return RISC_DMA_OK;
 }
 int32_t release(uint64_t scope,uint64_t token){
  if(!owner())return RISC_DMA_CONTEXT;
  size_t n=find(scope,token);if(n==kSlots)return RISC_DMA_STALE;
  auto&s=slots_[n];if(s.irq)return RISC_DMA_BUSY;
  int32_t status=backend_.release(n);if(!status)s={};return status;
 }
 int32_t irqClaim(uint64_t scope,uint64_t token,risc_dma_irq_handler_v1 fn,void*arg){
  if(!owner())return RISC_DMA_CONTEXT;
  size_t n=find(scope,token);if(n==kSlots)return RISC_DMA_STALE;
  auto&s=slots_[n];if(!s.ready||!fn||!backend_.irqClaim)return RISC_DMA_INVALID;
  if(s.irq)return RISC_DMA_BUSY;
  bool live=false;int32_t status=backend_.irqClaim(n,fn,arg,&live);s.irq=live;
  // A contradictory backend result is uncertainty, never successful admission.
  if(!status&&!live){s.irq=true;return RISC_DMA_PLATFORM;}
  return status;
 }
 int32_t irqEnable(uint64_t scope,uint64_t token,bool enable){
  if(!owner())return RISC_DMA_CONTEXT;
  size_t n=find(scope,token);if(n==kSlots)return RISC_DMA_STALE;
  if(!slots_[n].irq||!backend_.irqEnable)return RISC_DMA_INVALID;
  return backend_.irqEnable(n,enable);
 }
 int32_t irqRetire(uint64_t scope,uint64_t token){
  if(!owner())return RISC_DMA_CONTEXT;
  size_t n=find(scope,token);if(n==kSlots)return RISC_DMA_STALE;
  if(!slots_[n].irq||!backend_.irqRetire)return RISC_DMA_INVALID;
  int32_t status=backend_.irqRetire(n);if(!status)slots_[n].irq=false;return status;
 }
 private:
 struct Slot{uint64_t token=0,scope=0;bool ready=false,irq=false;};
 size_t find(uint64_t scope,uint64_t token)const{if(!token||!scope)return kSlots;for(size_t n=0;n<kSlots;++n)if(slots_[n].token==token&&slots_[n].scope==scope)return n;return kSlots;}
 Backend backend_;Slot slots_[kSlots]{};uint64_t serial_=0;
};
}
