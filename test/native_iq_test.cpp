#include "ports/esp32s3/NativeRadioIq.h"
#include <cassert>
#include <cstdio>
esp_chip_info_t test_chip{CHIP_ESP32S3,CHIP_FEATURE_WIFI_BGN|CHIP_FEATURE_BLE};
#ifndef IQ_ROM_CHIP
#define IQ_ROM_CHIP CHIP_ESP32S3
#endif
#ifndef IQ_ROM_ECO
#define IQ_ROM_ECO 0
#endif
extern "C" { extern const uint32_t _rom_chip_id=IQ_ROM_CHIP; extern const int32_t _rom_eco_version=IQ_ROM_ECO; }
static uint32_t run=0,banks=0,reads=0;
uint32_t iq_reg_read(uint32_t address){++reads;if(address==0x60033D5C)return run;assert(address==0x600C101C);return banks;}
int main(){
 using namespace RiscCpu::NativeRadioIq;
 if(IQ_ROM_CHIP!=CHIP_ESP32S3 || IQ_ROM_ECO<0){assert(!supported() && !ready() && !reads);puts("Native IQ: unsupported actual ROM rejected before MMIO PASS");return 0;}
#ifdef IQ_BAD_LAYOUT
 assert(supported() && !reserved() && !ready() && !reads);puts("Native IQ: static bank overlap rejected before MMIO PASS");return 0;
#endif
 assert(supported() && reserved() && ready());
 run=0x80000000;assert(!ready());run=0;
 for(unsigned b=0;b<4;++b){banks=1u<<b;assert(!ready());}
 banks=0x10;assert(ready());banks=0;
 test_chip.model=5;reads=0;assert(!ready() && !reads);test_chip.model=CHIP_ESP32S3;
 test_chip.features=CHIP_FEATURE_WIFI_BGN;assert(!ready() && !reads);test_chip.features|=CHIP_FEATURE_BLE;
 auto saved=reserved_region_risc_radio_iq;reserved_region_risc_radio_iq.end-=4;assert(!ready() && !reads);reserved_region_risc_radio_iq=saved;
 assert(ready());puts("Native IQ: actual chip gating before MMIO, exact pre-heap reservation, dump RUN and every bank bit, restored cleanup retry PASS");
}
