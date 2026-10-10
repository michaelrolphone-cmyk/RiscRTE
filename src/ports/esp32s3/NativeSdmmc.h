#pragma once
/* ESP32-S3 peripheral implementation. No FAT/VFS mount, board identity, power
 * rail, app pointer, or background worker is owned by this layer. */
#include <RiscGpioSdmmcV1.h>
#include <driver/gpio.h>
#include <driver/sdmmc_host.h>
#include <sdmmc_cmd.h>
#include <esp_attr.h>
#include <cstring>

namespace RiscCpu { namespace NativeSdmmc {
static bool (*owner)()=nullptr;
static bool attempted=false,installed=false,ready=false,busy=false,fault=false;
static int clk_pin=-1,cmd_pin=-1,dat0_pin=-1;
static sdmmc_card_t card{};
// A permanent internal buffer prevents stale DMA from retaining app/ELF
// memory, including on an SDK timeout. It is reused only after success.
DMA_ATTR static uint8_t dma[RISC_SDMMC_MAX_SECTORS*RISC_SDMMC_SECTOR_BYTES];
static void configure(bool (*check)()){owner=check;}
static bool owned(){return owner && owner();}
static bool idle(){return !busy && !fault;}
static bool closed(){return !attempted && !installed && !ready && !busy && !fault && clk_pin<0 && cmd_pin<0 && dat0_pin<0;}
static bool close(){
  if(!owned() || busy)return false;
  // IDF4 can fail init after enabling the peripheral, without an installed
  // transaction handler that its public deinit API can safely destroy.
  if(attempted && !installed)return false;
  ready=false;
  if(installed){
    if(sdmmc_host_deinit()!=ESP_OK){fault=true;return false;}
    installed=false;attempted=false;
  }
  int* pins[]={&clk_pin,&cmd_pin,&dat0_pin};
  for(int* pin:pins){
    if(*pin>=0){
      if(gpio_reset_pin(static_cast<gpio_num_t>(*pin))!=ESP_OK){fault=true;return false;}
      *pin=-1;
    }
  }
  card={};fault=false;return true;
}
static bool open(uint8_t clk,uint8_t cmd,uint8_t dat0,uint32_t hz,risc_sdmmc_card_info_v1* info){
  if(info)*info={};
  if(!owned() || !info || !closed() || hz!=RISC_SDMMC_MAX_HZ ||
     clk==cmd || clk==dat0 || cmd==dat0 || !GPIO_IS_VALID_OUTPUT_GPIO(clk) ||
     !GPIO_IS_VALID_OUTPUT_GPIO(cmd) || !GPIO_IS_VALID_OUTPUT_GPIO(dat0))return false;
  clk_pin=clk;cmd_pin=cmd;dat0_pin=dat0;attempted=true;fault=true;busy=true;
  const esp_err_t initialized=sdmmc_host_init();
  busy=false;
  if(initialized!=ESP_OK)return false;
  installed=true;
  sdmmc_host_t host=SDMMC_HOST_DEFAULT();
  host.flags=SDMMC_HOST_FLAG_1BIT;host.slot=SDMMC_HOST_SLOT_1;
  host.max_freq_khz=hz/1000u;host.command_timeout_ms=1000;
  sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width=1;slot.clk=static_cast<gpio_num_t>(clk);slot.cmd=static_cast<gpio_num_t>(cmd);slot.d0=static_cast<gpio_num_t>(dat0);
  slot.d1=slot.d2=slot.d3=slot.d4=slot.d5=slot.d6=slot.d7=GPIO_NUM_NC;
  slot.cd=SDMMC_SLOT_NO_CD;slot.wp=SDMMC_SLOT_NO_WP;slot.flags=SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
  busy=true;
  esp_err_t result=sdmmc_host_init_slot(host.slot,&slot);
  if(result==ESP_OK)result=sdmmc_card_init(&host,&card);
  busy=false;
  if(result!=ESP_OK || card.csd.sector_size!=RISC_SDMMC_SECTOR_BYTES || !card.csd.capacity ||
     card.max_freq_khz<=0 || card.max_freq_khz>host.max_freq_khz)return false;
  ready=true;fault=false;
  *info={sizeof(*info),RISC_SDMMC_SECTOR_BYTES,card.csd.capacity,uint32_t(card.max_freq_khz)*1000u,0};
  return true;
}
static bool valid(uint64_t lba,uint32_t count,const void* buffer){
  const auto address=reinterpret_cast<uintptr_t>(buffer);
  return owned() && ready && !busy && !fault && buffer && count && count<=RISC_SDMMC_MAX_SECTORS &&
    lba<SIZE_MAX && lba<card.csd.capacity && count<=uint64_t(card.csd.capacity)-lba &&
    count<=SIZE_MAX-lba && address<=UINTPTR_MAX-size_t(count)*RISC_SDMMC_SECTOR_BYTES;
}
static bool read(uint64_t lba,uint32_t count,void* out){
  if(!valid(lba,count,out))return false;
  busy=true;const esp_err_t result=sdmmc_read_sectors(&card,dma,size_t(lba),count);busy=false;
  if(result!=ESP_OK || !owned()){fault=true;return false;}
  std::memcpy(out,dma,size_t(count)*RISC_SDMMC_SECTOR_BYTES);return true;
}
static bool write(uint64_t lba,uint32_t count,const void* data){
  if(!valid(lba,count,data))return false;
  std::memcpy(dma,data,size_t(count)*RISC_SDMMC_SECTOR_BYTES);
  busy=true;const esp_err_t result=sdmmc_write_sectors(&card,dma,size_t(lba),count);busy=false;
  if(result!=ESP_OK || !owned()){fault=true;return false;}return true;
}
static bool sync(){
  if(!owned() || !ready || busy || fault)return false;
  busy=true;const esp_err_t result=sdmmc_get_status(&card);busy=false;
  if(result!=ESP_OK || !owned()){fault=true;return false;}return true;
}
} }
