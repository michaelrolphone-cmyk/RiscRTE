#include "ports/esp32s3/NativeSdmmc.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
namespace ns=RiscCpu::NativeSdmmc;
static bool allocation_bad=false;
static unsigned allocations=0,frees=0;
static bool owned=true,init_bad,slot_bad,card_bad,read_bad,write_bad,sync_bad,close_bad;
static int reset_bad=-1;
static unsigned inits,slots,cards,reads,writes,syncs,deinits,resets;
static uint8_t media[64*512];
void* heap_caps_malloc(size_t n,unsigned caps){assert(n==4096 && caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT));if(allocation_bad)return nullptr;++allocations;return std::malloc(n);}
void heap_caps_free(void* p){assert(p && p==ns::dma);++frees;std::free(p);}
static bool check_owner(){return owned;}
esp_err_t gpio_reset_pin(gpio_num_t pin){++resets;assert(pin==40 || pin==41 || pin==42);return int(pin)==reset_bad?ESP_FAIL:ESP_OK;}
esp_err_t sdmmc_host_init(){++inits;return init_bad?ESP_FAIL:ESP_OK;}
esp_err_t sdmmc_host_deinit(){++deinits;return close_bad?ESP_FAIL:ESP_OK;}
esp_err_t sdmmc_host_init_slot(int slot,const sdmmc_slot_config_t* c){
 ++slots;assert(slot==1 && c->width==1 && c->clk==41 && c->cmd==42 && c->d0==40);
 assert(c->d1==-1 && c->d2==-1 && c->d3==-1 && c->d4==-1 && c->d5==-1 && c->d6==-1 && c->d7==-1);
 assert(c->cd==-1 && c->wp==-1 && c->flags==SDMMC_SLOT_FLAG_INTERNAL_PULLUP);return slot_bad?ESP_FAIL:ESP_OK;
}
esp_err_t sdmmc_card_init(const sdmmc_host_t* h,sdmmc_card_t* c){
 ++cards;assert(h->flags==SDMMC_HOST_FLAG_1BIT && h->max_freq_khz==20000 && h->slot==1 && h->command_timeout_ms==1000);
 c->csd={512,64};c->max_freq_khz=20000;return card_bad?ESP_FAIL:ESP_OK;
}
esp_err_t sdmmc_read_sectors(sdmmc_card_t*,void* out,size_t lba,size_t count){
 ++reads;assert(out==ns::dma && !((uintptr_t)out%4));if(read_bad){memset(out,0xa5,count*512);return ESP_FAIL;}memcpy(out,media+lba*512,count*512);return ESP_OK;
}
esp_err_t sdmmc_write_sectors(sdmmc_card_t*,const void* in,size_t lba,size_t count){
 ++writes;assert(in==ns::dma && !((uintptr_t)in%4));if(write_bad)return ESP_FAIL;memcpy(media+lba*512,in,count*512);return ESP_OK;
}
esp_err_t sdmmc_get_status(sdmmc_card_t*){++syncs;return sync_bad?ESP_FAIL:ESP_OK;}
int main(int argc,char**argv){
 assert(argc==2);const char* mode=argv[1];ns::configure(check_owner);risc_sdmmc_card_info_v1 info{};assert(ns::closed() && ns::idle());
 owned=false;assert(!ns::open(41,42,40,20000000,&info) && !inits);owned=true;
 assert(!ns::open(41,42,40,40000000,&info) && !inits);assert(!ns::open(41,41,40,20000000,&info) && !inits);
 if(!strcmp(mode,"allocation-failure")){allocation_bad=true;assert(!ns::open(41,42,40,20000000,&info) && ns::closed() && !inits && !allocations && !frees);return 0;}
 if(!strcmp(mode,"init-failure")){init_bad=true;assert(!ns::open(41,42,40,20000000,&info));assert(!ns::close() && !ns::closed() && !ns::idle() && !deinits);return 0;}
 slot_bad=!strcmp(mode,"slot-failure");card_bad=!strcmp(mode,"card-failure");
 const bool opened=ns::open(41,42,40,20000000,&info);
 if(slot_bad || card_bad){assert(!opened && ns::close() && ns::closed() && resets==3);return 0;}
 assert(opened && !ns::closed() && ns::idle() && info.sector_size==512 && info.sector_count==64);
 uint8_t input[4097],output[4097];for(unsigned i=0;i<sizeof(input);++i)input[i]=uint8_t(i*3);
 assert(ns::write(56,8,input+1) && ns::read(56,8,output+1) && !memcmp(input+1,output+1,4096));assert(ns::sync());
 const unsigned before=reads+writes;assert(!ns::read(63,2,output) && !ns::read(0,9,output) && !ns::read(0,0,output));
 assert(!ns::read(UINT64_MAX,1,output) && !ns::read(0,1,nullptr) && reads+writes==before);
 if(!strcmp(mode,"read-failure")){read_bad=true;memset(output,0x34,sizeof(output));assert(!ns::read(0,1,output) && output[0]==0x34);assert(!ns::read(0,1,output) && !ns::write(0,1,input) && !ns::idle());}
 if(!strcmp(mode,"write-failure")){write_bad=true;assert(!ns::write(0,1,input));const auto n=writes;assert(!ns::write(0,1,input) && writes==n && !ns::idle());}
 if(!strcmp(mode,"sync-failure")){sync_bad=true;assert(!ns::sync() && !ns::idle());}
 if(!strcmp(mode,"close-failure")){close_bad=true;assert(!ns::close() && ns::installed && !ns::closed());close_bad=false;}
 if(!strcmp(mode,"reset-failure")){reset_bad=42;assert(!ns::close() && !ns::installed && ns::cmd_pin==42 && ns::clk_pin==-1);reset_bad=-1;}
 owned=false;assert(!ns::close());owned=true;assert(ns::close() && ns::closed());
 printf("Native SDMMC %s: one-bit/20MHz/exact pins, permanent DMA copy, bounds and teardown PASS\n",mode);
}
