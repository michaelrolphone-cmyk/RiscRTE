// Test-owned native startup. Build-only workflow; no normal startup replacement.
#include "ports/esp32s3/CpuPort.h"
#include <esp_private/gdma.h>
#include <hal/gdma_ll.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <cstdio>
#include "RiscBuildIdentity.h"
namespace {
TaskHandle_t ownerTask;
RiscDma::Reservation* dma;
RiscWorkers::ResidentWorker* worker;
uint64_t token,workerToken;
std::atomic<unsigned> irqCalls{0};
int32_t foreignResult=999;
gdma_channel_handle_t external[6]{};
bool connected[6]{};
bool owner(){return !xPortInIsrContext()&&xTaskGetCurrentTaskHandle()==ownerTask;}
[[noreturn]] void park(){for(;;)vTaskDelay(pdMS_TO_TICKS(1000));}
void require(bool ok,const char*reason){if(!ok){std::printf("DMA_PROBE result=unqualified reason=%s retained=1\n",reason);park();}}
void record(const char*name){std::printf("DMA_PROBE case=%s result=passed\n",name);}
void irq(void*){irqCalls.fetch_add(1,std::memory_order_relaxed);}
void foreign(void*,const risc_worker_control_v1*){foreignResult=dma->release(1,token);}
void releaseExternal(unsigned n){
 require(external[n]!=nullptr,"external_handle_missing");int group=-1,channel=-1;
 require(gdma_get_group_channel_id(external[n],&group,&channel)==ESP_OK&&group==0&&channel>=0&&channel<5,"external_channel_identity");
 auto*dev=GDMA_LL_GET_HW(0);
 require(gdma_ll_tx_is_desc_fsm_idle(dev,channel)&&!dev->channel[channel].out.int_ena.val&&!gdma_ll_tx_get_interrupt_status(dev,channel,true),"external_channel_not_idle");
 if(connected[n]){require(gdma_disconnect(external[n])==ESP_OK,"external_disconnect");connected[n]=false;}
 require(gdma_del_channel(external[n])==ESP_OK,"external_delete");external[n]=nullptr;
}
}
extern "C" void __wrap_app_main(){
 ownerTask=xTaskGetCurrentTaskHandle();
 std::printf("DMA_PROBE source=%s coverage=direct-resource-authority target_execution=1\n",RISC_BUILD_SOURCE_SHA);
 require(xTaskGetCoreID(ownerTask)==xPortGetCoreID(),"pinned_owner_precondition");
 const auto hardware=RiscCpu::nativeHardware(owner);dma=hardware.dmaReservation;worker=hardware.residentWorker;
 require(dma&&worker&&dma->owner()&&dma->idle()&&worker->idle(),"native_authority_admission");
 // Direct test-owned authority, not a selected board/provider route-policy proof.
 risc_dma_channel_v1 channel{sizeof(channel),0,0,0};token=99;
 require(dma->reserve(1,0,&token,&channel)==RISC_DMA_INVALID&&!token&&dma->idle(),"invalid_route");record("invalid-route");
 require(dma->reserve(1,RISC_DMA_LCD0,&token,&channel)==RISC_DMA_OK&&token,"lcd_reserve");
 require(channel.backend==RISC_DMA_ESP32S3_AHB&&channel.group==0&&channel.channel<5&&SYSTEM.perip_clk_en1.dma_clk_en,"identity_clock");record("reservation-identity-clock");
 require(dma->release(2,token)==RISC_DMA_STALE&&!dma->idle(),"foreign_scope");record("foreign-scope");
 require(worker->start(1,foreign,nullptr,4096,1,xPortGetCoreID(),&workerToken)==0,"foreign_worker_start");
 require(worker->join(1,workerToken,2000)==0,"foreign_worker_join");
 require(foreignResult==RISC_DMA_CONTEXT&&!dma->idle(),"foreign_owner");
 require(worker->release(1,workerToken)==0,"foreign_worker_release");workerToken=0;record("foreign-owner");
 require(dma->irqClaim(1,token,irq,nullptr)==RISC_DMA_OK,"irq_claim");
 require(!irqCalls.load()&&dma->release(1,token)==RISC_DMA_BUSY&&!dma->idle(),"irq_sublease_release_refusal");
 // Remains disabled: this is real allocation/free, not an observed fired ISR.
 require(dma->irqRetire(1,token)==RISC_DMA_OK&&!irqCalls.load()&&!dma->idle(),"irq_retire");record("disabled-irq-sublease-retirement");
 const auto stale=token;require(dma->release(1,token)==RISC_DMA_OK&&dma->idle(),"lcd_release");token=0;
 require(dma->release(1,stale)==RISC_DMA_STALE,"stale_release");
 require(dma->reserve(1,RISC_DMA_LCD0,&token,&channel)==RISC_DMA_OK&&token!=stale,"generation_renewal");
 require(dma->release(1,token)==0,"generation_release");token=0;record("stale-generation");
 gdma_channel_alloc_config_t config{};
 require(gdma_new_ahb_channel(&config,&external[0],nullptr)==ESP_OK&&external[0],"external_spi_allocate");
 gdma_trigger_t spi{};spi.instance_id=SOC_GDMA_TRIG_PERIPH_SPI2;spi.bus_id=SOC_GDMA_BUS_AHB;
 require(gdma_connect(external[0],spi)==ESP_OK,"external_spi_connect");connected[0]=true;
 require(dma->reserve(1,RISC_DMA_SPI2,&token,&channel)==RISC_DMA_PLATFORM&&!token&&dma->idle(),"shared_route_conflict");
 require(SYSTEM.perip_clk_en1.dma_clk_en,"external_clock_lost_after_conflict");
 require(dma->reserve(1,RISC_DMA_AES,&token,&channel)==RISC_DMA_OK,"independent_aes_route");
 require(dma->release(1,token)==0&&SYSTEM.perip_clk_en1.dma_clk_en,"external_clock_lost_after_release");token=0;
 releaseExternal(0);record("shared-route-clock-custody");
 unsigned count=0;esp_err_t exhausted=ESP_OK;
 for(;count<6;++count){
  exhausted=gdma_new_ahb_channel(&config,&external[count],nullptr);
  if(exhausted!=ESP_OK){require(!external[count],"failed_external_allocation_live_handle");break;}
  require(external[count]!=nullptr,"successful_external_allocation_null");
 }
 require(count<=5&&exhausted==ESP_ERR_NOT_FOUND,"actual_channel_exhaustion_not_observed");
 require(dma->reserve(1,RISC_DMA_LCD0,&token,&channel)==RISC_DMA_PLATFORM&&!token&&dma->idle(),"exhausted_reserve_retained_or_succeeded");
 for(unsigned n=0;n<count;++n)releaseExternal(n);
 require(dma->idle()&&worker->idle(),"final_authority_not_idle");record("shared-channel-exhaustion");
 std::printf("DMA_PROBE result=resource_cases_passed cases=8 fired_irq_observed=0 transfer_execution=0 physical_spi_crypto_concurrency=0 injected_failure=0\n");
 // Do not enter normal Runtime, display, storage or product startup. All callback
 // and result storage remains resident. Unknown cleanup parks with owned tokens.
 park();
}
