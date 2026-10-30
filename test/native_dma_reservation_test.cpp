#include "ports/esp32s3/NativeDmaReservation.h"
#include <cassert>
#include <cstdio>
DmaHw dmaHw{};
static bool isr=false,yieldable=true,owned=true,idle=true;static int scheduler=1,core=0,pinnedCore=0;
static unsigned raw=0,allocated=0,deleted=0,irqCalls=0;static int allocFailure=0,disconnectFailure=0,deleteFailure=0,irqFailure=0,disableFailure=0,freeFailure=0;
static bool nullAllocation=false,nullIrq=false,partialAllocation=false;static int queryFailure=0,connectFailure=0;static Channel channels[5];static bool used[5]{};static void*task=(void*)1;
bool xPortInIsrContext(){return isr;}bool xPortCanYield(){return yieldable;}int xTaskGetSchedulerState(){return scheduler;}
int xTaskGetCoreID(TaskHandle_t){return pinnedCore;}int xPortGetCoreID(){return core;}TaskHandle_t xTaskGetCurrentTaskHandle(){return task;}
int gdma_new_ahb_channel(const gdma_channel_alloc_config_t*,gdma_channel_handle_t*out,gdma_channel_handle_t*rx){assert(!rx);if(allocFailure)return -1;if(nullAllocation)return 0;for(int n=0;n<5;++n)if(!used[n]){used[n]=true;channels[n].id=n;*out=&channels[n];++allocated;return partialAllocation?-1:0;}return -1;}
int gdma_get_group_channel_id(gdma_channel_handle_t h,int*g,int*c){if(queryFailure)return queryFailure;*g=0;*c=h->id;return 0;}
int gdma_connect(gdma_channel_handle_t,gdma_trigger_t t){assert(t.bus_id==SOC_GDMA_BUS_AHB);return connectFailure;}
int gdma_disconnect(gdma_channel_handle_t){return disconnectFailure;}
int gdma_del_channel(gdma_channel_handle_t h){if(deleteFailure)return -1;assert(used[h->id]);used[h->id]=false;++deleted;return 0;}
bool gdma_ll_tx_is_desc_fsm_idle(DmaHw*,int){return idle;}unsigned gdma_ll_tx_get_interrupt_status(DmaHw*,int,bool r){assert(r);return raw;}
int esp_intr_alloc(int source,int flags,void(*)(void*),void*,intr_handle_t*out){assert(source==ETS_LCD_CAM_INTR_SOURCE&&flags==ESP_INTR_FLAG_INTRDISABLED);++irqCalls;if(!nullIrq)*out=(void*)2;return irqFailure;}
int esp_intr_enable(intr_handle_t){return 0;}int esp_intr_disable(intr_handle_t){return disableFailure;}int esp_intr_free(intr_handle_t){return freeFailure;}
static void handler(void*){}
int main(){using namespace RiscCpu::NativeDma;assert(configure([](){return owned;}));uint64_t t=0;risc_dma_channel_v1 c{sizeof(c),0,0,0};
 isr=true;assert(authority.reserve(1,RISC_DMA_LCD0,&t,&c)==RISC_DMA_CONTEXT&&!allocated);isr=false;
 yieldable=false;assert(authority.reserve(1,RISC_DMA_LCD0,&t,&c)==RISC_DMA_CONTEXT&&!allocated);yieldable=true;
 scheduler=0;assert(authority.reserve(1,RISC_DMA_LCD0,&t,&c)==RISC_DMA_CONTEXT&&!allocated);scheduler=1;
 allocFailure=-1;assert(authority.reserve(1,RISC_DMA_LCD0,&t,&c)==RISC_DMA_PLATFORM&&!t&&authority.idle());allocFailure=0;
 assert(authority.reserve(1,RISC_DMA_LCD0,&t,&c)==0&&t&&c.channel==0);
 idle=false;assert(authority.release(1,t)==RISC_DMA_BUSY&&!deleted);idle=true;
 dmaHw.channel[0].out.int_ena.val=1;assert(authority.release(1,t)==RISC_DMA_BUSY);dmaHw.channel[0].out.int_ena.val=0;
 raw=1;assert(authority.release(1,t)==RISC_DMA_BUSY);raw=0;
 pinnedCore=-1;assert(authority.irqClaim(1,t,handler,nullptr)==RISC_DMA_CONTEXT&&!irqCalls);pinnedCore=0;
 irqFailure=-1;assert(authority.irqClaim(1,t,handler,nullptr)==RISC_DMA_PLATFORM&&irqCalls==1);irqFailure=0;
 assert(authority.release(1,t)==RISC_DMA_BUSY&&!deleted);
 core=1;assert(authority.irqRetire(1,t)==RISC_DMA_CONTEXT);core=0;
 task=(void*)3;assert(authority.irqRetire(1,t)==RISC_DMA_CONTEXT);task=(void*)1;
 disableFailure=-1;assert(authority.irqRetire(1,t)==RISC_DMA_PLATFORM&&authority.release(1,t)==RISC_DMA_BUSY);disableFailure=0;
 freeFailure=-1;assert(authority.irqRetire(1,t)==RISC_DMA_PLATFORM&&authority.release(1,t)==RISC_DMA_BUSY);freeFailure=0;
 assert(authority.irqRetire(1,t)==0);
 disconnectFailure=-1;assert(authority.release(1,t)==RISC_DMA_PLATFORM&&!deleted);disconnectFailure=0;
 deleteFailure=-1;assert(authority.release(1,t)==RISC_DMA_PLATFORM&&!deleted);deleteFailure=0;
 assert(authority.release(1,t)==0&&deleted==1&&authority.idle());assert(authority.release(1,t)==RISC_DMA_STALE);
 // External SDK consumers occupy the same allocator, not core descriptors.
 gdma_channel_handle_t external[5]{};gdma_channel_alloc_config_t config{};for(auto&h:external)assert(gdma_new_ahb_channel(&config,&h,nullptr)==0);
 assert(authority.reserve(1,RISC_DMA_SPI2,&t,&c)==RISC_DMA_PLATFORM&&!t&&authority.idle());for(auto h:external)assert(gdma_del_channel(h)==0);
 assert(authority.reserve(1,RISC_DMA_AES,&t,&c)==0&&authority.release(1,t)==0);
 // Failure-with-live allocation is cleaned only when actual SDK cleanup succeeds.
 partialAllocation=true;deleteFailure=-1;
 assert(authority.reserve(1,RISC_DMA_LCD0,&t,&c)==RISC_DMA_PLATFORM&&t&&!authority.idle());
 partialAllocation=false;deleteFailure=0;assert(authority.release(1,t)==0);
 queryFailure=-1;assert(authority.reserve(1,RISC_DMA_LCD0,&t,&c)==RISC_DMA_PLATFORM&&t);
 queryFailure=0;assert(authority.release(1,t)==0);
 connectFailure=-1;assert(authority.reserve(1,RISC_DMA_LCD0,&t,&c)==RISC_DMA_PLATFORM&&!t&&authority.idle());connectFailure=0;
 // A reported-success/null IRQ keeps the descriptor and cannot silently retire.
 assert(authority.reserve(1,RISC_DMA_LCD0,&t,&c)==0);nullIrq=true;
 assert(authority.irqClaim(1,t,handler,nullptr)==RISC_DMA_PLATFORM);
 assert(authority.release(1,t)==RISC_DMA_BUSY&&authority.irqRetire(1,t)==RISC_DMA_PLATFORM);nullIrq=false;
 // Contradictory native success/null cannot be treated as clean rollback.
 nullAllocation=true;assert(authority.reserve(1,RISC_DMA_LCD0,&t,&c)==RISC_DMA_PLATFORM&&t&&!authority.idle());assert(authority.release(1,t)==RISC_DMA_PLATFORM);
 puts("Native DMA adapter: context, shared-allocation, transfer/IRQ fences and uncertain cleanup PASS (SDK mocked; no target execution)");
}
