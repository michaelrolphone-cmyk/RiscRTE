#include <cstdint>
constexpr int LEDC_LOW_SPEED_MODE=0,ESP_OK=0,SIG_GPIO_OUT_IDX=256;
using ledc_channel_t=int;
static int ledc_stop(int,ledc_channel_t,int){return ESP_OK;}
static void esp_rom_gpio_connect_out_signal(uint8_t,int,bool,bool){}
// The benchmark script selects the exact source implementation at each ref.
#include "NativePwmStop.inc"
static volatile uint64_t sdkWrites=0;
bool benchmarkNativeWrite(uint8_t pin,bool){if(!stopPwm(pin))return false;++sdkWrites;return true;}
void benchmarkNativeSetup(unsigned occupied){
 for(unsigned i=0;i<4;++i){pwmPins[i]=(occupied&(1u<<i))?41+i:-1;pwmOwnedPins[41+i]=pwmPins[i]>=0;}
}
uint64_t benchmarkNativeWrites(){return sdkWrites;}
