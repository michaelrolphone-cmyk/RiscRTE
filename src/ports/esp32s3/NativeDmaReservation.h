#pragma once
#include "runtime/dma/DmaReservation.h"
#include <esp_private/gdma.h>
#include <esp_intr_alloc.h>
#include <esp_attr.h>
#include <esp_idf_version.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal/gdma_ll.h>
#include <soc/interrupts.h>
#include <sdkconfig.h>
#if !CONFIG_IDF_TARGET_ESP32S3 || ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(6,1,0)
#error "Native DMA reservation requires reviewed ESP32-S3 IDF6.1"
#endif
static_assert(GDMA_LL_AHB_NUM_GROUPS==1&&GDMA_LL_AHB_PAIRS_PER_GROUP==5,"Unreviewed DMA topology");
namespace RiscCpu::NativeDma {
struct Route {int trigger,irq;};
static constexpr Route routes[]={
 {SOC_GDMA_TRIG_PERIPH_SPI2,ETS_SPI2_INTR_SOURCE},{SOC_GDMA_TRIG_PERIPH_SPI3,ETS_SPI3_INTR_SOURCE},
 {SOC_GDMA_TRIG_PERIPH_UHCI0,ETS_UHCI0_INTR_SOURCE},{SOC_GDMA_TRIG_PERIPH_I2S0,ETS_I2S0_INTR_SOURCE},
 {SOC_GDMA_TRIG_PERIPH_I2S1,ETS_I2S1_INTR_SOURCE},{SOC_GDMA_TRIG_PERIPH_LCD0,ETS_LCD_CAM_INTR_SOURCE},
 {SOC_GDMA_TRIG_PERIPH_AES0,ETS_AES_INTR_SOURCE},{SOC_GDMA_TRIG_PERIPH_SHA0,ETS_SHA_INTR_SOURCE},
 {SOC_GDMA_TRIG_PERIPH_RMT0,ETS_RMT_INTR_SOURCE}};
struct Slot {gdma_channel_handle_t handle=nullptr;int group=-1,channel=-1;uint32_t route=0;bool connected=false,exposed=false,allocationUnknown=false;intr_handle_t irq=nullptr;TaskHandle_t task=nullptr;BaseType_t core=-1;bool irqUnknown=false;};
static DRAM_ATTR Slot slots[RiscDma::Reservation::kSlots];
static bool (*ownerCheck)();
static bool owner(){return !xPortInIsrContext()&&xPortCanYield()&&xTaskGetSchedulerState()==taskSCHEDULER_RUNNING&&ownerCheck&&ownerCheck();}
static bool pinned(TaskHandle_t task,BaseType_t core){return task&&core>=0&&core<2&&xTaskGetCoreID(task)==core;}
static bool irqOwner(const Slot&s){return owner()&&s.task==xTaskGetCurrentTaskHandle()&&s.core==xPortGetCoreID()&&pinned(s.task,s.core);}
static int32_t release(size_t n){
 auto&s=slots[n];if(s.allocationUnknown)return RISC_DMA_PLATFORM;if(s.irq||s.irqUnknown)return RISC_DMA_BUSY;
 if(s.handle){
  // No IRQ callbacks are registered on the opaque GDMA handle. The only IRQ
  // sublease is separately owned below, so SDK deletion never frees an IRQ.
  {
   if(s.group!=0||s.channel<0||s.channel>=5){if(gdma_get_group_channel_id(s.handle,&s.group,&s.channel)!=ESP_OK||s.group!=0||s.channel<0||s.channel>=5)return RISC_DMA_PLATFORM;}
   auto*dev=GDMA_LL_GET_HW(0);
   if(!gdma_ll_tx_is_desc_fsm_idle(dev,s.channel)||dev->channel[s.channel].out.int_ena.val||gdma_ll_tx_get_interrupt_status(dev,s.channel,true))return RISC_DMA_BUSY;
  }
  if(s.connected){if(gdma_disconnect(s.handle)!=ESP_OK)return RISC_DMA_PLATFORM;s.connected=false;}
  if(gdma_del_channel(s.handle)!=ESP_OK)return RISC_DMA_PLATFORM;
 }
 s={};return RISC_DMA_OK;
}
static int32_t reserve(size_t n,uint32_t route,risc_dma_channel_v1*out){
 if(route<1||route>sizeof(routes)/sizeof(*routes))return RISC_DMA_INVALID;
 auto&s=slots[n];if(s.handle||s.irq||s.irqUnknown)return RISC_DMA_BUSY;
 s.route=route;gdma_channel_alloc_config_t config{};
 if(gdma_new_ahb_channel(&config,&s.handle,nullptr)!=ESP_OK)return RISC_DMA_PLATFORM;
 if(!s.handle){s.allocationUnknown=true;return RISC_DMA_PLATFORM;}
 if(gdma_get_group_channel_id(s.handle,&s.group,&s.channel)!=ESP_OK||s.group!=0||s.channel<0||s.channel>=5)return RISC_DMA_PLATFORM;
 gdma_trigger_t trigger{};trigger.instance_id=routes[route-1].trigger;trigger.bus_id=SOC_GDMA_BUS_AHB;
 if(gdma_connect(s.handle,trigger)!=ESP_OK)return RISC_DMA_PLATFORM;
 s.connected=true;s.exposed=true;*out={sizeof(*out),RISC_DMA_ESP32S3_AHB,uint32_t(s.group),uint32_t(s.channel)};return RISC_DMA_OK;
}
static int32_t irqClaim(size_t n,risc_dma_irq_handler_v1 handler,void*arg,bool*live){
 auto&s=slots[n];*live=s.irq||s.irqUnknown;if(*live)return RISC_DMA_BUSY;
 TaskHandle_t task=xTaskGetCurrentTaskHandle();BaseType_t core=xPortGetCoreID();
 if(!owner()||!pinned(task,core))return RISC_DMA_CONTEXT;
 if(!s.exposed||!s.route||!handler)return RISC_DMA_INVALID;
 // Same pinned-owner/core mechanism as existing NativeIrqLifecycle. No shared,
 // IRAM, arbitrary-source or raw interrupt handle authority is exported.
 intr_handle_t irq=nullptr;esp_err_t result=esp_intr_alloc(routes[s.route-1].irq,ESP_INTR_FLAG_INTRDISABLED,handler,arg,&irq);
 if(irq||result==ESP_OK){s.irq=irq;s.task=task;s.core=core;s.irqUnknown=!irq;*live=true;}
 return result==ESP_OK&&irq?RISC_DMA_OK:RISC_DMA_PLATFORM;
}
static int32_t irqEnable(size_t n,bool enable){
 auto&s=slots[n];if(!irqOwner(s))return RISC_DMA_CONTEXT;
 if(!s.irq||s.irqUnknown)return RISC_DMA_PLATFORM;
 return (enable?esp_intr_enable(s.irq):esp_intr_disable(s.irq))==ESP_OK?RISC_DMA_OK:RISC_DMA_PLATFORM;
}
static int32_t irqRetire(size_t n){
 auto&s=slots[n];if(!irqOwner(s))return RISC_DMA_CONTEXT;
 if(!s.irq||s.irqUnknown)return RISC_DMA_PLATFORM;
 if(esp_intr_disable(s.irq)!=ESP_OK)return RISC_DMA_PLATFORM;
 if(esp_intr_free(s.irq)!=ESP_OK)return RISC_DMA_PLATFORM;
 s.irq=nullptr;s.task=nullptr;s.core=-1;return RISC_DMA_OK;
}
static DRAM_ATTR RiscDma::Reservation authority({owner,reserve,release,irqClaim,irqEnable,irqRetire});
static bool configure(bool(*check)()){if(!authority.idle())return ownerCheck==check;ownerCheck=check;return check!=nullptr;}
}
